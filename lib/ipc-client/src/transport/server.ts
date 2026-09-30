import * as net from "node:net";
import {
    failure,
    makeError,
    normalizeError,
    success,
    type HHError,
    type Result,
} from "../error.js";
import { Connection } from "./connection.js";
import { endpoint } from "./endpoint.js";

type AcceptWaiter = (result: Result<Connection>) => void;

export class Server {
    readonly #server: net.Server;
    readonly #pending: Result<Connection>[] = [];
    readonly #waiters: AcceptWaiter[] = [];
    readonly #closeErrors: HHError[] = [];

    #error: HHError | undefined;
    #closed = false;
    #closing = false;
    #closeTask: Promise<Result<void>> | undefined;

    constructor(server: net.Server) {
        this.#server = server;

        server.on("connection", (socket) => {
            const connection = Connection.create(socket);
            if (this.#closing || this.#closed) {
                if (connection.error !== null) {
                    this.#closeErrors.push(connection.error);
                } else {
                    const cleanup = connection.value.destroy();
                    if (cleanup.error !== null) {
                        this.#closeErrors.push(cleanup.error);
                    }
                }
                return;
            }

            const waiter = this.#waiters.shift();
            if (waiter !== undefined) {
                waiter(connection);
            } else {
                this.#pending.push(connection);
            }
        });
        server.on("error", (error: unknown) => {
            const serverError = normalizeError(error, "accept", "system_error", [
                { api: "net.Server", event: "error" },
            ]);
            this.#error ??= serverError;
            this.#finishWaiters(failure(serverError));
        });
        server.on("close", () => {
            this.#closed = true;
            this.#finishWaiters(failure(this.#error ?? this.#unavailableError()));
        });
    }

    accept(): Promise<Result<Connection>> {
        if (this.#closing || this.#closed) {
            return Promise.resolve(failure(this.#error ?? this.#unavailableError()));
        }
        const connection = this.#pending.shift();
        if (connection !== undefined) {
            return Promise.resolve(connection);
        }
        if (this.#error !== undefined) {
            return Promise.resolve(failure(this.#error));
        }
        return new Promise<Result<Connection>>((resolve) => {
            this.#waiters.push(resolve);
        });
    }

    close(): Promise<Result<void>> {
        if (this.#closeTask !== undefined) {
            return this.#closeTask;
        }
        if (this.#closed) {
            return Promise.resolve(success(undefined));
        }

        this.#closing = true;
        this.#finishWaiters(failure(this.#error ?? this.#unavailableError()));
        for (const connection of this.#pending.splice(0)) {
            if (connection.error !== null) {
                this.#closeErrors.push(connection.error);
            } else {
                const cleanup = connection.value.destroy();
                if (cleanup.error !== null) {
                    this.#closeErrors.push(cleanup.error);
                }
            }
        }

        this.#closeTask = new Promise<Result<void>>((resolve) => {
            const complete = (error?: unknown) => {
                if (error !== undefined && error !== null) {
                    this.#closeErrors.push(normalizeError(error, "close", "system_error", [
                        { api: "net.Server.close" },
                    ]));
                }
                if (this.#closeErrors.length === 0) {
                    resolve(success(undefined));
                } else if (this.#closeErrors.length === 1) {
                    resolve(failure(this.#closeErrors[0]!));
                } else {
                    resolve(failure(makeError(
                        "close", "dependency_error", "Server shutdown encountered multiple failures", [],
                        [...this.#closeErrors],
                    )));
                }
            };
            try {
                this.#server.close(complete);
            } catch (error) {
                complete(error);
            }
        });
        return this.#closeTask;
    }

    #unavailableError(): HHError {
        return makeError("accept", "state_error", this.#closed ? "IPC server is closed" : "IPC server is closing");
    }

    #finishWaiters(result: Result<Connection>): void {
        for (const waiter of this.#waiters.splice(0)) {
            waiter(result);
        }
    }
}

export async function listen(name: string): Promise<Result<Server>> {
    const path = endpoint(name, "listen");
    if (path.error !== null) {
        return failure(path.error);
    }

    let server: net.Server;
    try {
        server = net.createServer({ allowHalfOpen: false, pauseOnConnect: false });
    } catch (error) {
        return failure(normalizeError(error, "listen", "system_error", [
            { api: "net.createServer", path: path.value },
        ]));
    }
    const result = new Server(server);

    const listening = await new Promise<Result<void>>((resolve) => {
        const finish = (outcome: Result<void>) => {
            server.off("error", onError);
            server.off("listening", onListening);
            resolve(outcome);
        };
        const onError = (error: unknown) => finish(failure(normalizeError(error, "listen", "system_error", [
            { api: "net.Server.listen", path: path.value },
        ])));
        const onListening = () => finish(success(undefined));

        server.once("error", onError);
        server.once("listening", onListening);
        try {
            server.listen(path.value);
        } catch (error) {
            onError(error);
        }
    });

    if (listening.error !== null) {
        if (server.listening) {
            const cleanup = await result.close();
            if (cleanup.error !== null) {
                return failure(makeError(
                    "listen", "dependency_error", "Server startup and cleanup failed", [],
                    [listening.error, cleanup.error],
                ));
            }
        }
        return failure(listening.error);
    }
    return success(result);
}
