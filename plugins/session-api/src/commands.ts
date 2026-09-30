import { failure, makeError, normalizeError, success, type Result } from "./error.js";

export type UInt64 = number | bigint;
export type CommandId = UInt64;

export type SandboxConfig = {
    read_only: string[];
    read_write: string[];
    network: "none" | "internet_client";
};

export type SessionConfig = {
    api_key_raw: string;
    history: unknown[];
    session_current: unknown;
    tool_definitions: unknown[];
    provider: string;
    endpoint: string;
    model_id: string;
    context_limit: UInt64;
    compact_threshold: UInt64;
    tool_result_timeout_ms: number;
    session_timeout_ms: number;
    compaction_prompt: string;
    workspace_path: string;
    tool_runtime_executable: string;
    sandbox_config: SandboxConfig;
    refresh_workspace: boolean;
};

type SessionConfigWire = Omit<SessionConfig, "context_limit" | "compact_threshold"> & {
    context_limit: bigint;
    compact_threshold: bigint;
};

export type SessionCommand =
    | readonly [bigint, 1, SessionConfigWire]
    | readonly [bigint, 2]
    | readonly [bigint, 3]
    | readonly [bigint, 4]
    | readonly [bigint, 5]
    | readonly [bigint, 6]
    | readonly [bigint, 7]
    | readonly [bigint, 8];

const maxUInt64 = (1n << 64n) - 1n;

export function isUInt64(value: unknown): value is UInt64 {
    return (typeof value === "bigint" && value >= 0n && value <= maxUInt64)
        || (typeof value === "number" && Number.isSafeInteger(value) && value >= 0);
}

export function uint64(
    value: UInt64,
    operation = "uint64",
    field = "value",
    positive = false,
): Result<bigint> {
    if (!isUInt64(value) || (positive && value === (typeof value === "bigint" ? 0n : 0))) {
        return failure(makeError(operation, "validation_error",
            positive ? "Value must be a positive uint64" : "Value must be a uint64",
            [{ field, value: typeof value === "bigint" ? value.toString() : value }]));
    }
    try {
        return success(typeof value === "bigint" ? value : BigInt(value));
    } catch (error) {
        return failure(normalizeError(error, operation, "validation_error", [{ field, value }]));
    }
}

export function registerSession(commandId: CommandId, config: SessionConfig): Result<SessionCommand> {
    const id = uint64(commandId, "register_session", "command_id", true);
    if (id.error !== null) return failure(id.error);
    try {
        const contextLimit = uint64(config.context_limit, "register_session", "context_limit", true);
        if (contextLimit.error !== null) return failure(contextLimit.error);
        const compactThreshold = uint64(config.compact_threshold, "register_session", "compact_threshold");
        if (compactThreshold.error !== null) return failure(compactThreshold.error);
        return success([id.value, 1, {
            ...config,
            context_limit: contextLimit.value,
            compact_threshold: compactThreshold.value,
        }]);
    } catch (error) {
        return failure(normalizeError(error, "register_session", "validation_error"));
    }
}

function command(commandId: CommandId, kind: 2 | 3 | 4 | 5 | 6 | 7 | 8, operation: string): Result<SessionCommand> {
    const id = uint64(commandId, operation, "command_id", true);
    return id.error === null ? success([id.value, kind]) : failure(id.error);
}

export function declareRequest(id: CommandId): Result<SessionCommand> { return command(id, 2, "declare_request"); }
export function runRequest(id: CommandId): Result<SessionCommand> { return command(id, 3, "run_request"); }
export function declareResponse(id: CommandId): Result<SessionCommand> { return command(id, 4, "declare_response"); }
export function runResponse(id: CommandId): Result<SessionCommand> { return command(id, 5, "run_response"); }
export function declareTool(id: CommandId): Result<SessionCommand> { return command(id, 6, "declare_tool"); }
export function runTool(id: CommandId): Result<SessionCommand> { return command(id, 7, "run_tool"); }
export function closeSession(id: CommandId): Result<SessionCommand> { return command(id, 8, "close"); }
