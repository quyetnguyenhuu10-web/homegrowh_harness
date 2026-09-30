import * as net from "node:net";
import {
    failure,
    makeError,
    normalizeError,
    success,
    type HHError,
    type Result,
} from "../error.js";
import { endpoint } from "./endpoint.js";

const headerSize = 4;
export const maxFrameSize = 16 * 1024 * 1024;

function asBuffer(data: Uint8Array): Buffer {
    return Buffer.isBuffer(data)
        ? data
        : Buffer.from(data.buffer, data.byteOffset, data.byteLength);
}

export class Connection {
    readonly #socket: net.Socket;
    readonly #chunks: Buffer[] = [];

    #headOffset = 0;
    #available = 0;
    #ended = false;
    #closed = false;
    #readError: HHError | undefined;
    #writeError: HHError | undefined;
    #readWaiter: (() => void) | undefined;
    #readInFlight = false;
    #writeTail: Promise<void> = Promise.resolve();

    private constructor(socket: net.Socket) {
        this.#socket = socket;

        socket.on("data", (chunk: Buffer) => {
            if (chunk.length > 0) {
                this.#chunks.push(chunk);
                this.#available += chunk.length;
                this.#wakeReader();
            }
        });
        socket.on("end", () => {
            this.#ended = true;
            this.#wakeReader();
        });
        socket.on("error", (error: unknown) => {
            this.#rememberError(normalizeError(error, "socket", "system_error", [
                { api: "net.Socket", event: "error" },
            ]));
        });
        socket.on("close", () => {
            this.#closed = true;
            this.#wakeReader();
        });
    }

    static create(socket: net.Socket): Result<Connection> {
        const connection = new Connection(socket);
        try {
            socket.setNoDelay(true);
            return success(connection);
        } catch (error) {
            const setupError = normalizeError(error, "configure_connection", "system_error", [
                { api: "net.Socket.setNoDelay" },
            ]);
            const cleanup = connection.destroy();
            return failure(cleanup.error === null ? setupError : makeError(
                "configure_connection",
                "dependency_error",
                "Connection setup and cleanup failed",
                [],
                [setupError, cleanup.error],
            ));
        }
    }

    async read(): Promise<Result<Buffer | null>> {
        if (this.#readInFlight) {
            return failure(makeError("read", "state_error", "IPC allows only one read at a time"));
        }

        this.#readInFlight = true;
        try {
            const header = await this.#readExact(headerSize, true);
            if (header.error !== null) {
                return failure(header.error);
            }
            if (header.value === null) {
                return success(null);
            }

            const size = header.value.readUInt32LE(0);
            if (size > maxFrameSize) {
                return failure(makeError("read", "protocol_error", "IPC frame exceeds the size limit", [
                    { size, max_frame_size: maxFrameSize },
                ]));
            }
            if (size === 0) {
                return success(Buffer.alloc(0));
            }

            return await this.#readExact(size, false);
        } catch (error) {
            return failure(normalizeError(error, "read", "protocol_error"));
        } finally {
            this.#readInFlight = false;
        }
    }

    write(data: Uint8Array): Promise<Result<void>> {
        try {
            if (data.byteLength > maxFrameSize) {
                return Promise.resolve(failure(makeError(
                    "write", "protocol_error", "IPC frame exceeds the size limit",
                    [{ size: data.byteLength, max_frame_size: maxFrameSize }],
                )));
            }

            const payload = asBuffer(data);
            const write = this.#writeTail.then(() => this.#writeFrame(payload));
            this.#writeTail = write.then(() => undefined);
            return write;
        } catch (error) {
            return Promise.resolve(failure(normalizeError(error, "write", "protocol_error")));
        }
    }

    close(): Result<void> {
        try {
            this.#socket.end();
            return success(undefined);
        } catch (error) {
            const closeError = normalizeError(error, "close", "system_error", [
                { api: "net.Socket.end" },
            ]);
            this.#rememberError(closeError);
            return failure(closeError);
        }
    }

    destroy(): Result<void> {
        try {
            this.#socket.destroy();
            return success(undefined);
        } catch (error) {
            const destroyError = normalizeError(error, "destroy", "system_error", [
                { api: "net.Socket.destroy" },
            ]);
            this.#rememberError(destroyError);
            return failure(destroyError);
        }
    }

    async #readExact(size: number, cleanCloseAllowed: boolean): Promise<Result<Buffer | null>> {
        while (this.#available < size) {
            if (this.#readError !== undefined) {
                return failure(this.#readError);
            }
            if (this.#ended || this.#closed) {
                if (cleanCloseAllowed && this.#ended && this.#available === 0) {
                    return success(null);
                }
                return failure(makeError("read", "protocol_error", "IPC peer closed before a complete frame", [
                    { expected_size: size, available_size: this.#available, ended: this.#ended, closed: this.#closed },
                ]));
            }
            await new Promise<void>((resolve) => {
                this.#readWaiter = resolve;
            });
        }
        return this.#take(size);
    }

    #take(size: number): Result<Buffer> {
        const first = this.#chunks[0];
        if (first === undefined) {
            return this.#bufferError(size);
        }
        const firstAvailable = first.length - this.#headOffset;

        if (firstAvailable >= size) {
            const result = first.subarray(this.#headOffset, this.#headOffset + size);
            this.#headOffset += size;
            this.#available -= size;
            this.#advanceChunk(first);
            return success(result);
        }

        const result = Buffer.allocUnsafe(size);
        let outputOffset = 0;
        while (outputOffset < size) {
            const chunk = this.#chunks[0];
            if (chunk === undefined) {
                return this.#bufferError(size);
            }
            const copySize = Math.min(size - outputOffset, chunk.length - this.#headOffset);
            chunk.copy(result, outputOffset, this.#headOffset, this.#headOffset + copySize);
            outputOffset += copySize;
            this.#headOffset += copySize;
            this.#available -= copySize;
            this.#advanceChunk(chunk);
        }
        return success(result);
    }

    #bufferError(size: number): Result<never> {
        return failure(makeError("read", "internal_error", "IPC receive buffer invariant violated", [
            { expected_size: size, available_size: this.#available, head_offset: this.#headOffset },
        ]));
    }

    #advanceChunk(chunk: Buffer): void {
        if (this.#headOffset === chunk.length) {
            this.#chunks.shift();
            this.#headOffset = 0;
        }
    }

    async #writeFrame(payload: Buffer): Promise<Result<void>> {
        if (this.#writeError !== undefined) {
            return failure(this.#writeError);
        }
        if (this.#closed || this.#socket.destroyed) {
            return failure(makeError("write", "state_error", "IPC connection is closed"));
        }

        try {
            const header = Buffer.allocUnsafe(headerSize);
            header.writeUInt32LE(payload.length, 0);

            return await new Promise<Result<void>>((resolve) => {
                let corked = false;
                let api = "net.Socket.cork";
                let setupComplete = false;
                let outcome: Result<void> | undefined;
                const complete = (error?: unknown) => {
                    if (outcome !== undefined) {
                        return;
                    }
                    if (error !== undefined && error !== null) {
                        const writeError = normalizeError(error, "write", "system_error", [
                            { api: "net.Socket.write" },
                        ]);
                        this.#rememberError(writeError);
                        outcome = failure(writeError);
                    } else {
                        outcome = this.#writeError === undefined ? success(undefined) : failure(this.#writeError);
                    }
                    if (setupComplete) {
                        resolve(outcome);
                    }
                };

                try {
                    this.#socket.cork();
                    corked = true;
                    api = "net.Socket.write";
                    this.#socket.write(header);
                    this.#socket.write(payload, complete);
                } catch (error) {
                    const writeError = normalizeError(error, "write", "system_error", [{ api }]);
                    this.#rememberError(writeError);
                    outcome = failure(writeError);
                } finally {
                    if (corked) {
                        try {
                            this.#socket.uncork();
                        } catch (error) {
                            const uncorkError = normalizeError(error, "write", "system_error", [
                                { api: "net.Socket.uncork" },
                            ]);
                            const writeError = outcome?.error ? makeError(
                                "write", "dependency_error", "Frame write and uncork failed", [],
                                [outcome.error, uncorkError],
                            ) : uncorkError;
                            const previousError = this.#writeError;
                            if (this.#readError === previousError) {
                                this.#readError = writeError;
                            }
                            this.#writeError = writeError;
                            this.#wakeReader();
                            outcome = failure(writeError);
                        }
                    }
                    setupComplete = true;
                    if (outcome !== undefined) {
                        resolve(outcome);
                    }
                }
            });
        } catch (error) {
            return failure(normalizeError(error, "write", "protocol_error"));
        }
    }

    #rememberError(error: HHError): void {
        this.#readError ??= error;
        this.#writeError ??= error;
        this.#wakeReader();
    }

    #wakeReader(): void {
        const waiter = this.#readWaiter;
        this.#readWaiter = undefined;
        waiter?.();
    }
}

export async function connect(name: string): Promise<Result<Connection>> {
    const path = endpoint(name, "connect");
    if (path.error !== null) {
        return failure(path.error);
    }

    let socket: net.Socket;
    try {
        socket = new net.Socket();
    } catch (error) {
        return failure(normalizeError(error, "connect", "system_error", [
            { api: "net.Socket", path: path.value },
        ]));
    }

    const connection = Connection.create(socket);
    if (connection.error !== null) {
        return failure(connection.error);
    }

    const connected = await new Promise<Result<void>>((resolve) => {
        const finish = (result: Result<void>) => {
            socket.off("error", onError);
            socket.off("connect", onConnect);
            socket.off("close", onClose);
            resolve(result);
        };
        const onError = (error: unknown) => finish(failure(normalizeError(error, "connect", "system_error", [
            { api: "net.Socket.connect", path: path.value },
        ])));
        const onConnect = () => finish(success(undefined));
        const onClose = () => finish(failure(makeError("connect", "state_error", "IPC socket closed before connecting", [
            { api: "net.Socket", event: "close", path: path.value },
        ])));

        socket.once("error", onError);
        socket.once("connect", onConnect);
        socket.once("close", onClose);
        try {
            socket.connect(path.value);
        } catch (error) {
            onError(error);
        }
    });

    if (connected.error !== null) {
        const cleanup = connection.value.destroy();
        return failure(cleanup.error === null ? connected.error : makeError(
            "connect", "dependency_error", "Connection attempt and cleanup failed", [],
            [connected.error, cleanup.error],
        ));
    }
    return connection;
}
