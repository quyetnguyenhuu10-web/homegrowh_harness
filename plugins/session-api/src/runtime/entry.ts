import type { IpcConnection } from "@hh/ipc-client";
import type { UInt64 } from "../commands.js";
import { combineFailures, failure, success, type HHError, type Result } from "../error.js";
import type { RuntimeCloseResult, RuntimeProcess } from "./process.js";

export type CommandResult = {
    command_id: UInt64;
    state: string;
    result: unknown;
};

export type RuntimeEntry = {
    connection: IpcConnection;
    process: RuntimeProcess;
    server_closing: Promise<Result<void>>;
    raw_events: unknown[];
    stream_waiters: Array<(result: Result<unknown | null>) => void>;
    error_events: HHError[];
    error_waiters: Array<(result: Result<HHError | null>) => void>;
    command_waiters: Map<bigint, (result: Result<CommandResult>) => void>;
    reader: Promise<Result<void>>;
    reader_error: HHError | null;
    failures: HHError[];
    reader_closed: boolean;
    abort_requested: boolean;
    fatal_error: HHError | null;
    fatal_errors: HHError[];
    closing: boolean;
    close_task?: Promise<Result<RuntimeCloseResult>>;
    stop_process_observer: () => void;
};

export function failCommands(entry: RuntimeEntry, error: HHError): void {
    for (const waiter of entry.command_waiters.values()) waiter(failure(error));
    entry.command_waiters.clear();
}

export function recordFailure(entry: RuntimeEntry, error: HHError): HHError {
    if (!entry.failures.includes(error)) entry.failures.push(error);
    entry.reader_error = combineFailures("read_events", "Runtime processing encountered multiple failures", entry.failures)!;
    failCommands(entry, entry.reader_error);
    for (const waiter of entry.stream_waiters.splice(0)) waiter(failure(entry.reader_error));
    for (const waiter of entry.error_waiters.splice(0)) waiter(failure(entry.reader_error));
    return entry.reader_error;
}

export function finishStreams(entry: RuntimeEntry): void {
    entry.reader_closed = true;
    for (const waiter of entry.stream_waiters.splice(0)) waiter(success(null));
    for (const waiter of entry.error_waiters.splice(0)) waiter(success(null));
}

export function pushEvent(entry: RuntimeEntry, raw: unknown): void {
    const waiter = entry.stream_waiters.shift();
    if (waiter !== undefined) waiter(success(raw));
    else entry.raw_events.push(raw);
}

export function pushError(entry: RuntimeEntry, error: HHError): void {
    const waiter = entry.error_waiters.shift();
    if (waiter !== undefined) waiter(success(error));
    else entry.error_events.push(error);
}

export function nextEvent(entry: RuntimeEntry): Promise<Result<unknown | null>> {
    if (entry.raw_events.length > 0) return Promise.resolve(success(entry.raw_events.shift()));
    if (entry.reader_error !== null) return Promise.resolve(failure(entry.reader_error));
    if (entry.reader_closed) return Promise.resolve(success(null));
    return new Promise((resolve) => { entry.stream_waiters.push(resolve); });
}

export function nextError(entry: RuntimeEntry): Promise<Result<HHError | null>> {
    if (entry.error_events.length > 0) return Promise.resolve(success(entry.error_events.shift()!));
    if (entry.reader_error !== null) return Promise.resolve(failure(entry.reader_error));
    if (entry.reader_closed) return Promise.resolve(success(null));
    return new Promise((resolve) => { entry.error_waiters.push(resolve); });
}
