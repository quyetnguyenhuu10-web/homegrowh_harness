import * as net from "node:net";

const headerSize = 4;

export const maxFrameSize = 16 * 1024 * 1024;

function endpoint(name: string): string {
    if (
        name.length === 0 ||
        name.includes("\0") ||
        name.includes("/") ||
        name.includes("\\")
    ) {
        throw new TypeError("ipc name is invalid");
    }

    if (process.platform === "win32") {
        return `\\\\.\\pipe\\${name}`;
    }

    return `/tmp/${name}.sock`;
}

function asBuffer(data: Uint8Array): Buffer {
    if (Buffer.isBuffer(data)) {
        return data;
    }

    return Buffer.from(
        data.buffer,
        data.byteOffset,
        data.byteLength,
    );
}

export class Connection {
    readonly #socket: net.Socket;
    readonly #chunks: Buffer[] = [];

    #headOffset = 0;
    #available = 0;
    #ended = false;
    #closed = false;
    #readError: Error | undefined;
    #writeError: Error | undefined;
    #readWaiter: (() => void) | undefined;
    #readInFlight = false;
    #writeTail: Promise<void> = Promise.resolve();

    constructor(socket: net.Socket) {
        this.#socket = socket;
        socket.setNoDelay(true);

        socket.on("data", (chunk: Buffer) => {
            if (chunk.length === 0) {
                return;
            }

            this.#chunks.push(chunk);
            this.#available += chunk.length;
            this.#wakeReader();
        });

        socket.on("end", () => {
            this.#ended = true;
            this.#wakeReader();
        });

        socket.on("error", (error: Error) => {
            this.#readError ??= error;
            this.#writeError ??= error;
            this.#wakeReader();
        });

        socket.on("close", () => {
            this.#closed = true;
            if (!this.#ended && this.#readError === undefined) {
                this.#readError = new Error(
                    "ipc connection closed before peer end",
                );
            }
            this.#wakeReader();
        });
    }

    async read(): Promise<Buffer | null> {
        if (this.#readInFlight) {
            throw new Error("ipc allows only one read at a time");
        }

        this.#readInFlight = true;
        try {
            const header = await this.#readExact(headerSize, true);
            if (header === null) {
                return null;
            }

            const size = header.readUInt32LE(0);
            if (size > maxFrameSize) {
                throw new RangeError(
                    `ipc frame exceeds ${maxFrameSize} bytes`,
                );
            }

            if (size === 0) {
                return Buffer.alloc(0);
            }

            const payload = await this.#readExact(size, false);
            if (payload === null) {
                throw new Error("ipc peer closed inside a frame");
            }
            return payload;
        } finally {
            this.#readInFlight = false;
        }
    }

    write(data: Uint8Array): Promise<void> {
        if (data.byteLength > maxFrameSize) {
            return Promise.reject(
                new RangeError(
                    `ipc frame exceeds ${maxFrameSize} bytes`,
                ),
            );
        }

        const payload = asBuffer(data);
        const write = this.#writeTail.then(() => this.#writeFrame(payload));
        this.#writeTail = write.catch(() => undefined);
        return write;
    }

    close(): void {
        this.#socket.end();
    }

    destroy(): void {
        this.#socket.destroy();
    }

    async #readExact(
        size: number,
        cleanCloseAllowed: boolean,
    ): Promise<Buffer | null> {
        while (this.#available < size) {
            if (this.#readError !== undefined) {
                throw this.#readError;
            }

            if (this.#ended || this.#closed) {
                if (cleanCloseAllowed && this.#available === 0) {
                    return null;
                }
                throw new Error("ipc peer closed inside a frame");
            }

            await new Promise<void>((resolve) => {
                this.#readWaiter = resolve;
            });
        }

        return this.#take(size);
    }

    #take(size: number): Buffer {
        const first = this.#chunks[0];
        if (first === undefined) {
            throw new Error("ipc receive buffer invariant violated");
        }
        const firstAvailable = first.length - this.#headOffset;

        if (firstAvailable >= size) {
            const result = first.subarray(
                this.#headOffset,
                this.#headOffset + size,
            );

            this.#headOffset += size;
            this.#available -= size;

            if (this.#headOffset === first.length) {
                this.#chunks.shift();
                this.#headOffset = 0;
            }

            return result;
        }

        const result = Buffer.allocUnsafe(size);
        let outputOffset = 0;

        while (outputOffset < size) {
            const chunk = this.#chunks[0];
            if (chunk === undefined) {
                throw new Error("ipc receive buffer invariant violated");
            }
            const available = chunk.length - this.#headOffset;
            const copySize = Math.min(size - outputOffset, available);

            chunk.copy(
                result,
                outputOffset,
                this.#headOffset,
                this.#headOffset + copySize,
            );

            outputOffset += copySize;
            this.#headOffset += copySize;
            this.#available -= copySize;

            if (this.#headOffset === chunk.length) {
                this.#chunks.shift();
                this.#headOffset = 0;
            }
        }

        return result;
    }

    #writeFrame(payload: Buffer): Promise<void> {
        if (this.#writeError !== undefined) {
            return Promise.reject(this.#writeError);
        }
        if (this.#closed || this.#socket.destroyed) {
            return Promise.reject(new Error("ipc connection is closed"));
        }

        const header = Buffer.allocUnsafe(headerSize);
        header.writeUInt32LE(payload.length, 0);

        return new Promise<void>((resolve, reject) => {
            const complete = (error?: Error | null) => {
                if (error !== undefined && error !== null) {
                    this.#writeError ??= error;
                    reject(error);
                    return;
                }
                resolve();
            };

            this.#socket.cork();
            this.#socket.write(header);

            if (payload.length === 0) {
                this.#socket.write(Buffer.alloc(0), complete);
            } else {
                this.#socket.write(payload, complete);
            }

            this.#socket.uncork();
        });
    }

    #wakeReader(): void {
        const waiter = this.#readWaiter;
        this.#readWaiter = undefined;
        waiter?.();
    }
}

type AcceptWaiter = {
    resolve: (connection: Connection) => void;
    reject: (error: Error) => void;
};

export class Server {
    readonly #server: net.Server;
    readonly #pending: Connection[] = [];
    readonly #waiters: AcceptWaiter[] = [];

    #error: Error | undefined;
    #closed = false;

    constructor(server: net.Server) {
        this.#server = server;

        server.on("connection", (socket) => {
            const connection = new Connection(socket);
            const waiter = this.#waiters.shift();

            if (waiter !== undefined) {
                waiter.resolve(connection);
                return;
            }

            this.#pending.push(connection);
        });

        server.on("error", (error: Error) => {
            this.#error ??= error;
            this.#rejectWaiters(error);
        });

        server.on("close", () => {
            this.#closed = true;
            this.#rejectWaiters(new Error("ipc server is closed"));
        });
    }

    accept(): Promise<Connection> {
        const connection = this.#pending.shift();
        if (connection !== undefined) {
            return Promise.resolve(connection);
        }

        if (this.#error !== undefined) {
            return Promise.reject(this.#error);
        }

        if (this.#closed) {
            return Promise.reject(new Error("ipc server is closed"));
        }

        return new Promise<Connection>((resolve, reject) => {
            this.#waiters.push({ resolve, reject });
        });
    }

    close(): Promise<void> {
        if (this.#closed) {
            return Promise.resolve();
        }

        return new Promise<void>((resolve, reject) => {
            this.#server.close((error?: Error) => {
                if (error !== undefined) {
                    reject(error);
                    return;
                }
                resolve();
            });
        });
    }

    #rejectWaiters(error: Error): void {
        for (;;) {
            const waiter = this.#waiters.shift();
            if (waiter === undefined) {
                return;
            }
            waiter.reject(error);
        }
    }
}

export async function listen(name: string): Promise<Server> {
    const server = net.createServer({
        allowHalfOpen: false,
        pauseOnConnect: false,
    });

    const result = new Server(server);
    const path = endpoint(name);

    await new Promise<void>((resolve, reject) => {
        const onError = (error: Error) => {
            server.off("listening", onListening);
            reject(error);
        };
        const onListening = () => {
            server.off("error", onError);
            resolve();
        };

        server.once("error", onError);
        server.once("listening", onListening);
        server.listen(path);
    });

    return result;
}

export async function connect(name: string): Promise<Connection> {
    const socket = net.createConnection(endpoint(name));

    await new Promise<void>((resolve, reject) => {
        const onError = (error: Error) => {
            socket.off("connect", onConnect);
            reject(error);
        };
        const onConnect = () => {
            socket.off("error", onError);
            resolve();
        };

        socket.once("error", onError);
        socket.once("connect", onConnect);
    });

    return new Connection(socket);
}
