import { spawn } from "node:child_process";
import type { ChildProcess } from "node:child_process";
import { listen, type IpcConnection, type IpcServer } from "@hh/ipc-client";
import { uint64, type UInt64 } from "../commands.js";
import { parseEvent, type RuntimeEvent } from "../events.js";
import { combineFailures, failure, makeError, normalizeError, success, type HHError, type Result } from "../error.js";
import { failCommands, nextError, nextEvent, recordFailure, type CommandResult, type RuntimeEntry } from "./entry.js";
import { RuntimeProcess, type RuntimeCloseResult } from "./process.js";
import { processFailure, readEvents } from "./reader.js";

export type RuntimeDependencies = {
    listen: (name: string) => Promise<Result<IpcServer>>;
    spawn: (executable: string, pipeName: string) => Result<ChildProcess>;
};

const defaultDependencies: RuntimeDependencies = {
    listen,
    spawn(executable, pipeName) {
        try {
            return success(spawn(executable, [pipeName], {
                stdio: ["ignore", "inherit", "inherit"],
                windowsHide: true,
            }));
        } catch (error) {
            return failure(normalizeError(error, "open_runtime", "system_error",
                [{ api: "child_process.spawn", path: executable, args: [pipeName] }]));
        }
    },
};

async function closeServer(server: IpcServer): Promise<Result<void>> {
    try {
        return await server.close();
    } catch (error) {
        return failure(normalizeError(error, "close_runtime", "system_error", [{ api: "IpcServer.close" }]));
    }
}

export class RuntimeHost {
    readonly #dependencies: RuntimeDependencies;
    readonly #runtimes = new Map<bigint, RuntimeEntry>();
    #nextRuntimeId = 1n;

    constructor(dependencies: RuntimeDependencies = defaultDependencies) {
        this.#dependencies = dependencies;
    }

    async openRuntime(executable: string): Promise<Result<{ runtime_id: bigint }>> {
        if (typeof executable !== "string" || executable.length === 0 || executable.includes("\0")) {
            return failure(makeError("open_runtime", "validation_error", "Runtime executable path is invalid", [{ path: executable }]));
        }
        const id = this.#nextRuntimeId++;
        const pipeName = `hh-session-runtime-${process.pid}-${id}`;
        let server: IpcServer | undefined;
        let processMonitor: RuntimeProcess | undefined;
        let connection: IpcConnection | undefined;
        try {
            const listening = await this.#dependencies.listen(pipeName);
            if (listening.error !== null) return failure(listening.error);
            server = listening.value;
            const spawned = this.#dependencies.spawn(executable, pipeName);
            if (spawned.error !== null) return this.#cleanupOpen(spawned.error, server);
            processMonitor = new RuntimeProcess(spawned.value);
            const accepted = await Promise.race([server.accept(), processMonitor.beforeConnect]);
            if (accepted.error !== null) return this.#cleanupOpen(accepted.error, server, processMonitor);
            connection = accepted.value;
            processMonitor.connected();
            const entry: RuntimeEntry = {
                connection, process: processMonitor, server_closing: closeServer(server),
                raw_events: [], stream_waiters: [], error_events: [], error_waiters: [],
                command_waiters: new Map(), reader: Promise.resolve(success(undefined)),
                reader_error: null, failures: [], reader_closed: false, abort_requested: false,
                fatal_error: null, fatal_errors: [], closing: false, stop_process_observer: () => {},
            };
            entry.stop_process_observer = processMonitor.onFailure((error) => processFailure(entry, error));
            this.#runtimes.set(id, entry);
            entry.reader = readEvents(entry);
            return success({ runtime_id: id });
        } catch (error) {
            return this.#cleanupOpen(normalizeError(error, "open_runtime", "system_error"), server, processMonitor, connection);
        }
    }

    async sendRuntime(id: UInt64, message: unknown): Promise<Result<null>> {
        const found = this.#runtime(id, "send");
        if (found.error !== null) return failure(found.error);
        const usable = this.#usable(found.value, "send");
        if (usable !== null) return failure(usable);
        try {
            const sent = await found.value.connection.send(message);
            return sent.error === null ? success(null) : failure(sent.error);
        } catch (error) {
            return failure(normalizeError(error, "send", "system_error"));
        }
    }

    async receiveRuntime(id: UInt64): Promise<Result<RuntimeEvent | null>> {
        const received = await this.streamRuntimeEvent(id);
        if (received.error !== null) return failure(received.error);
        return received.value === null ? success(null) : parseEvent(received.value);
    }

    async streamRuntimeEvent(id: UInt64): Promise<Result<unknown | null>> {
        const found = this.#runtime(id, "stream_event");
        return found.error === null ? nextEvent(found.value) : failure(found.error);
    }

    async streamRuntimeError(id: UInt64): Promise<Result<HHError | null>> {
        const found = this.#runtime(id, "stream_error");
        return found.error === null ? nextError(found.value) : failure(found.error);
    }

    async executeRuntimeCommand(runtimeId: UInt64, commandId: UInt64, message: unknown): Promise<Result<CommandResult>> {
        const found = this.#runtime(runtimeId, "execute_command");
        if (found.error !== null) return failure(found.error);
        const entry = found.value;
        const usable = this.#usable(entry, "execute_command");
        if (usable !== null) return failure(usable);
        const id = uint64(commandId, "execute_command", "command_id", true);
        if (id.error !== null) return failure(id.error);
        if (entry.command_waiters.has(id.value)) {
            return failure(makeError("execute_command", "state_error", "Command id is already pending",
                [{ command_id: id.value.toString() }]));
        }
        const completed = new Promise<Result<CommandResult>>((resolve) => {
            entry.command_waiters.set(id.value, resolve);
        });
        const sent = await this.sendRuntime(runtimeId, message);
        if (sent.error !== null) {
            entry.command_waiters.delete(id.value);
            return failure(sent.error);
        }
        return completed;
    }

    async closeRuntime(runtimeId: UInt64): Promise<Result<RuntimeCloseResult>> {
        const id = uint64(runtimeId, "close_runtime", "runtime_id", true);
        if (id.error !== null) return failure(id.error);
        const found = this.#runtime(id.value, "close_runtime");
        if (found.error !== null) return failure(found.error);
        const entry = found.value;
        if (entry.close_task !== undefined) return entry.close_task;
        entry.closing = true;
        failCommands(entry, entry.reader_error ?? entry.fatal_error ?? makeError("close_runtime", "state_error",
            "Runtime is closing", [{ runtime_id: id.value.toString() }]));
        entry.close_task = this.#close(id.value, entry);
        const result = await entry.close_task;
        if (this.#runtimes.get(id.value) === entry) entry.close_task = undefined;
        return result;
    }

    #runtime(runtimeId: UInt64, operation: string): Result<RuntimeEntry> {
        const id = uint64(runtimeId, operation, "runtime_id", true);
        if (id.error !== null) return failure(id.error);
        const entry = this.#runtimes.get(id.value);
        return entry === undefined
            ? failure(makeError(operation, "state_error", "Runtime id is not open", [{ runtime_id: id.value.toString() }]))
            : success(entry);
    }

    #usable(entry: RuntimeEntry, operation: string): HHError | null {
        if (entry.reader_error !== null) return entry.reader_error;
        if (entry.fatal_error !== null) return entry.fatal_error;
        if (entry.closing || entry.reader_closed) {
            return makeError(operation, "state_error", entry.closing ? "Runtime is closing" : "Runtime event stream is closed");
        }
        return null;
    }

    async #cleanupOpen(
        original: HHError,
        server?: IpcServer,
        processMonitor?: RuntimeProcess,
        connection?: IpcConnection,
    ): Promise<Result<never>> {
        const errors = [original];
        if (connection !== undefined) {
            const destroyed = connection.destroyTransport();
            if (destroyed.error !== null) errors.push(destroyed.error);
        }
        if (processMonitor !== undefined) {
            const terminated = processMonitor.terminate();
            if (terminated.error !== null) errors.push(terminated.error);
            else {
                const exited = await processMonitor.waitForTermination();
                if (exited.error !== null) errors.push(exited.error);
            }
        }
        if (server !== undefined) {
            const closed = await closeServer(server);
            if (closed.error !== null) errors.push(closed.error);
        }
        return failure(combineFailures("open_runtime", "Runtime startup and cleanup encountered failures", errors)!);
    }

    async #close(id: bigint, entry: RuntimeEntry): Promise<Result<RuntimeCloseResult>> {
        const errors: HHError[] = [];
        const closed = entry.connection.closeTransport();
        let transportCleanupFailed = false;
        let terminationFailed = false;
        if (closed.error !== null) {
            errors.push(closed.error);
            recordFailure(entry, closed.error);
            entry.abort_requested = true;
            const destroyed = entry.connection.destroyTransport();
            if (destroyed.error !== null) {
                errors.push(destroyed.error);
                transportCleanupFailed = true;
            }
            const terminated = entry.process.terminate();
            if (terminated.error !== null) {
                errors.push(terminated.error);
                terminationFailed = true;
            }
        } else if (entry.reader_error !== null) {
            const terminated = entry.process.terminate();
            if (terminated.error !== null) {
                errors.push(terminated.error);
                terminationFailed = true;
            }
        }
        const exited = terminationFailed ? null : await entry.process.waitForTermination();
        if (exited?.error) errors.push(exited.error);
        if (!transportCleanupFailed) {
            const serverClosed = await entry.server_closing;
            if (serverClosed.error !== null) errors.push(serverClosed.error);
            const reader = await entry.reader;
            if (reader.error !== null) errors.push(reader.error);
        }
        if (entry.reader_error !== null) errors.push(entry.reader_error);
        if (entry.fatal_error !== null) errors.push(entry.fatal_error);
        const error = combineFailures("close_runtime", "Runtime shutdown encountered multiple failures", errors);
        if (!entry.process.running && !transportCleanupFailed) {
            entry.stop_process_observer();
            this.#runtimes.delete(id);
        }
        return error === null ? success(exited!.value!) : failure(error);
    }
}
