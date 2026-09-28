import * as ipc from "../ipc/index.js";
import {
    decode,
    encode,
} from "./codec.js";

export class SessionClient {
    readonly #connection: ipc.Connection;

    constructor(connection: ipc.Connection) {
        this.#connection = connection;
    }

    async send(value: unknown): Promise<void> {
        await this.#connection.write(encode(value));
    }

    async receive(): Promise<unknown | null> {
        const frame = await this.#connection.read();
        if (frame === null) {
            return null;
        }

        return decode(frame);
    }

    async *messages(): AsyncGenerator<unknown, void, void> {
        for (;;) {
            const message = await this.receive();
            if (message === null) {
                return;
            }
            yield message;
        }
    }

    closeTransport(): void {
        this.#connection.close();
    }

    destroyTransport(): void {
        this.#connection.destroy();
    }
}

export class SessionClientServer {
    readonly #server: ipc.Server;

    constructor(server: ipc.Server) {
        this.#server = server;
    }

    async accept(): Promise<SessionClient> {
        return new SessionClient(await this.#server.accept());
    }

    close(): Promise<void> {
        return this.#server.close();
    }
}

export async function listen(
    name: string,
): Promise<SessionClientServer> {
    return new SessionClientServer(await ipc.listen(name));
}

export async function connect(name: string): Promise<SessionClient> {
    return new SessionClient(await ipc.connect(name));
}
