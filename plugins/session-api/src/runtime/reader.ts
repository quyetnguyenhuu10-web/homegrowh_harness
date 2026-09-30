import { uint64 } from "../commands.js";
import { eventError, parseEvent, type RuntimeEvent } from "../events.js";
import { combineFailures, failure, makeError, normalizeError, success, type HHError, type Result } from "../error.js";
import { failCommands, finishStreams, pushError, pushEvent, recordFailure, type RuntimeEntry } from "./entry.js";

function dispatchCommand(entry: RuntimeEntry, event: RuntimeEvent): void {
    const error = eventError(event);
    if (event.kind === "runtime_failed" || event.kind === "protocol_error") {
        // The parser guarantees HHError on failure events.
        if (error !== null) {
            if (!entry.fatal_errors.includes(error)) entry.fatal_errors.push(error);
            entry.fatal_error = combineFailures("dispatch_command", "Runtime reported multiple fatal failures", entry.fatal_errors);
            failCommands(entry, entry.fatal_error!);
        }
        return;
    }
    if (event.kind !== "command_finished" && event.kind !== "command_failed") return;
    const id = uint64(event.command_id!, "dispatch_command", "command_id", true);
    if (id.error !== null) {
        recordFailure(entry, id.error);
        return;
    }
    const waiter = entry.command_waiters.get(id.value);
    if (waiter === undefined) return;
    entry.command_waiters.delete(id.value);
    if (event.kind === "command_failed") {
        waiter(failure(error!));
    } else {
        waiter(success({ command_id: event.command_id!, state: event.state!, result: event.result }));
    }
}

function abortReader(entry: RuntimeEntry): void {
    entry.abort_requested = true;
    const cleanup = entry.connection.destroyTransport();
    if (cleanup.error !== null) recordFailure(entry, cleanup.error);
}

export function processFailure(entry: RuntimeEntry, error: HHError): void {
    recordFailure(entry, error);
    abortReader(entry);
}

export async function readEvents(entry: RuntimeEntry): Promise<Result<void>> {
    try {
        for (;;) {
            const received = await entry.connection.receive();
            if (received.error !== null) {
                if (entry.abort_requested && entry.reader_error !== null) return failure(entry.reader_error);
                const error = recordFailure(entry, received.error);
                abortReader(entry);
                return failure(entry.reader_error ?? error);
            }
            if (received.value === null) {
                entry.reader_closed = true;
                if (entry.reader_error !== null) return failure(entry.reader_error);
                if (entry.command_waiters.size !== 0) {
                    return failure(recordFailure(entry, makeError("read_events", "state_error",
                        "Runtime closed with pending commands",
                        [{ command_ids: [...entry.command_waiters.keys()].map((id) => id.toString()) }])));
                }
                finishStreams(entry);
                return success(undefined);
            }
            const parsed = parseEvent(received.value);
            if (parsed.error !== null) {
                recordFailure(entry, parsed.error);
                abortReader(entry);
                return failure(entry.reader_error!);
            }
            pushEvent(entry, received.value);
            const error = eventError(parsed.value);
            if (error !== null) pushError(entry, error);
            dispatchCommand(entry, parsed.value);
        }
    } catch (value) {
        recordFailure(entry, normalizeError(value, "read_events", "system_error"));
        abortReader(entry);
        return failure(entry.reader_error!);
    }
}
