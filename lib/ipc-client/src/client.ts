import * as ipc from "./ipc.js";
import {
    decode,
    encode,
} from "./codec.js";
import { failure, success, type Result } from "./error.js";

export class IpcConnection {
    readonly #connection: ipc.Connection;

    constructor(connection: ipc.Connection) {
        this.#connection = connection;
    }

    async send(value: unknown): Promise<Result<void>> {
        const encoded = encode(value);
        if (encoded.error !== null) {
            return failure(encoded.error);
        }
        return this.#connection.write(encoded.value);
    }

    async receive(): Promise<Result<unknown | null>> {
        const frame = await this.#connection.read();
        if (frame.error !== null) {
            return failure(frame.error);
        }
        if (frame.value === null) {
            return success(null);
        }

        return decode(frame.value);
    }

    async *messages(): AsyncGenerator<Result<unknown>, void, void> {
        for (;;) {
            const message = await this.receive();
            if (message.error !== null) {
                yield message;
                return;
            }
            if (message.value === null) {
                return;
            }
            yield message;
        }
    }

    closeTransport(): Result<void> {
        return this.#connection.close();
    }

    destroyTransport(): Result<void> {
        return this.#connection.destroy();
    }
}

export class IpcServer {
    readonly #server: ipc.Server;

    constructor(server: ipc.Server) {
        this.#server = server;
    }

    async accept(): Promise<Result<IpcConnection>> {
        const connection = await this.#server.accept();
        return connection.error === null
            ? success(new IpcConnection(connection.value))
            : failure(connection.error);
    }

    close(): Promise<Result<void>> {
        return this.#server.close();
    }
}

export async function listen(
    name: string,
): Promise<Result<IpcServer>> {
    const server = await ipc.listen(name);
    return server.error === null ? success(new IpcServer(server.value)) : failure(server.error);
}

export async function connect(name: string): Promise<Result<IpcConnection>> {
    const connection = await ipc.connect(name);
    return connection.error === null
        ? success(new IpcConnection(connection.value))
        : failure(connection.error);
}
