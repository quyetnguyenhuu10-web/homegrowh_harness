import { isUInt64 } from "./commands.js";
import type { UInt64 } from "./commands.js";
import { failure, isHHError, makeError, normalizeError, success, type HHError, type Result } from "./error.js";

export type EventReference = { type: string; value: string };

export type RuntimeEventKind =
    | "event"
    | "ready"
    | "protocol_error"
    | "runtime_failed"
    | "command_finished"
    | "command_failed"
    | "provider_http_error"
    | "provider_failed"
    | "session_failed"
    | "session_secondary_error";

export type RuntimeEvent = {
    kind: RuntimeEventKind;
    sequence: UInt64;
    timestamp_ms: UInt64;
    source: string;
    level: number;
    type: string;
    references: EventReference[];
    data: unknown;
    command_id?: UInt64;
    state?: string;
    result?: unknown;
    protocol_version?: number;
    exit_code?: number;
};

export type EventParseResult = Result<RuntimeEvent>;

export const EventLevel = {
    trace: 0, debug: 1, info: 2, warning: 3, error: 4, critical: 5,
} as const;

function objectData(value: unknown): Record<string, unknown> | null {
    return typeof value === "object" && value !== null && !Array.isArray(value)
        ? value as Record<string, unknown> : null;
}

export function eventError(event: RuntimeEvent): HHError | null {
    const payload = objectData(event.data);
    return payload !== null && isHHError(payload.error) ? payload.error : null;
}

export function isErrorEvent(event: RuntimeEvent): boolean {
    return event.level >= EventLevel.error || eventError(event) !== null;
}

function invalid(raw: unknown, path: string, message: string, value: unknown): Result<never> {
    return failure(makeError("parse_event", "protocol_error", message,
        [{ schema_errors: [{ path, message, value }], raw }]));
}

function parseReferences(value: unknown, raw: unknown): Result<EventReference[]> {
    if (!Array.isArray(value)) {
        return invalid(raw, "references", "Event references must be an array", value);
    }
    const result: EventReference[] = [];
    for (let index = 0; index < value.length; index++) {
        const reference = value[index];
        if (!Array.isArray(reference) || reference.length !== 2
            || typeof reference[0] !== "string" || typeof reference[1] !== "string") {
            return invalid(raw, `references[${index}]`, "Invalid event reference", reference);
        }
        result.push({ type: reference[0], value: reference[1] });
    }
    return success(result);
}

function eventKind(source: string, type: string): RuntimeEventKind {
    if (source === "provider") {
        if (type === "http_error") return "provider_http_error";
        if (type === "failed") return "provider_failed";
    }
    if (source === "sessions") {
        if (type === "failed") return "session_failed";
        if (type === "secondary_error") return "session_secondary_error";
    }
    if (source === "session_runtime") {
        switch (type) {
            case "ready":
            case "protocol_error":
            case "runtime_failed":
            case "command_finished":
            case "command_failed":
                return type;
        }
    }
    return "event";
}

const failureKinds = new Set<RuntimeEventKind>([
    "protocol_error", "runtime_failed", "command_failed", "provider_http_error",
    "provider_failed", "session_failed", "session_secondary_error",
]);

export function parseEvent(raw: unknown): EventParseResult {
    try {
        return parseWireEvent(raw);
    } catch (error) {
        return failure(normalizeError(error, "parse_event", "protocol_error", [{ raw }]));
    }
}

function parseWireEvent(raw: unknown): EventParseResult {
    if (!Array.isArray(raw) || raw.length !== 7) {
        return invalid(raw, "", "Invalid EventPort wire event", raw);
    }
    const [sequence, timestampMs, source, level, type, references, data] = raw;
    if (!isUInt64(sequence)) return invalid(raw, "sequence", "Event sequence must be uint64", sequence);
    if (!isUInt64(timestampMs)) return invalid(raw, "timestamp_ms", "Event timestamp_ms must be uint64", timestampMs);
    if (typeof source !== "string") return invalid(raw, "source", "Event source must be a string", source);
    if (typeof level !== "number" || !Number.isInteger(level)) {
        return invalid(raw, "level", "Event level must be an integer", level);
    }
    if (typeof type !== "string") return invalid(raw, "type", "Event type must be a string", type);
    const parsedReferences = parseReferences(references, raw);
    if (parsedReferences.error !== null) return failure(parsedReferences.error);

    const kind = eventKind(source, type);
    const event: RuntimeEvent = {
        kind, sequence, timestamp_ms: timestampMs, source, level, type,
        references: parsedReferences.value, data,
    };
    const payload = objectData(data);
    const hasError = payload !== null && Object.hasOwn(payload, "error");
    if (level >= EventLevel.error || failureKinds.has(kind) || hasError) {
        if (payload === null || !isHHError(payload.error)) {
            return invalid(raw, "data.error", "Error event data.error must be HHError", payload?.error);
        }
    }

    if (kind === "command_finished" || kind === "command_failed") {
        if (payload === null || !isUInt64(payload.command_id) || payload.command_id === 0 || payload.command_id === 0n) {
            return invalid(raw, "data.command_id", "Command event must contain a positive uint64 command_id", payload?.command_id);
        }
        event.command_id = payload.command_id;
        if (kind === "command_finished") {
            if (typeof payload.state !== "string") {
                return invalid(raw, "data.state", "command_finished event must contain a state", payload.state);
            }
            event.state = payload.state;
            if (!Object.hasOwn(payload, "result") || payload.result === undefined) {
                return invalid(raw, "data.result", "command_finished event must contain a result", payload.result);
            }
            event.result = payload.result;
        }
    }
    if (kind === "ready" && payload !== null && Object.hasOwn(payload, "protocol_version")) {
        if (typeof payload.protocol_version !== "number" || !Number.isInteger(payload.protocol_version) || payload.protocol_version < 1) {
            return invalid(raw, "data.protocol_version", "Protocol version must be a positive integer", payload.protocol_version);
        }
        event.protocol_version = payload.protocol_version;
    }
    if (kind === "runtime_failed" && payload !== null && Object.hasOwn(payload, "exit_code")) {
        if (typeof payload.exit_code !== "number" || !Number.isInteger(payload.exit_code)) {
            return invalid(raw, "data.exit_code", "Runtime exit_code must be an integer", payload.exit_code);
        }
        event.exit_code = payload.exit_code;
    }
    return success(event);
}
