import type {
    Plugin,
    PluginRequest,
} from "@hh/plugin-loader";

type UInt64 = number | bigint;
type CommandId = UInt64;

const Api = {
    register: 1,
    declareRequest: 2,
    runRequest: 3,
    declareResponse: 4,
    runResponse: 5,
    declareTool: 6,
    runTool: 7,
    close: 8,
} as const;

type SandboxConfig = {
    read_only: string[];
    read_write: string[];
    network: "none" | "internet_client";
};

type SessionConfig = {
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

type RegisterSessionCommand = readonly [
    commandId: bigint,
    opcode: 1,
    config: SessionConfigWire,
];

type DeclareRequestCommand = readonly [
    commandId: bigint,
    opcode: 2,
];

type RunRequestCommand = readonly [
    commandId: bigint,
    opcode: 3,
];

type DeclareResponseCommand = readonly [
    commandId: bigint,
    opcode: 4,
];

type RunResponseCommand = readonly [
    commandId: bigint,
    opcode: 5,
];

type DeclareToolCommand = readonly [
    commandId: bigint,
    opcode: 6,
];

type RunToolCommand = readonly [
    commandId: bigint,
    opcode: 7,
];

type CloseCommand = readonly [
    commandId: bigint,
    opcode: 8,
];

type SessionCommand =
    | RegisterSessionCommand
    | DeclareRequestCommand
    | RunRequestCommand
    | DeclareResponseCommand
    | RunResponseCommand
    | DeclareToolCommand
    | RunToolCommand
    | CloseCommand;

type SessionConfigWire = Omit<
    SessionConfig,
    "context_limit" | "compact_threshold"
> & {
    context_limit: bigint;
    compact_threshold: bigint;
};

function uint64(value: UInt64): bigint {
    return typeof value === "bigint"
        ? value
        : BigInt(value);
}

function sessionConfigWire(config: SessionConfig): SessionConfigWire {
    return {
        ...config,
        context_limit: uint64(config.context_limit),
        compact_threshold: uint64(config.compact_threshold),
    };
}

function register(
    commandId: CommandId,
    config: SessionConfig,
): RegisterSessionCommand {
    return [
        uint64(commandId),
        1,
        sessionConfigWire(config),
    ];
}

function declareRequest(
    commandId: CommandId,
): DeclareRequestCommand {
    return [uint64(commandId), 2];
}

function runRequest(
    commandId: CommandId,
): RunRequestCommand {
    return [uint64(commandId), 3];
}

function declareResponse(
    commandId: CommandId,
): DeclareResponseCommand {
    return [uint64(commandId), 4];
}

function runResponse(
    commandId: CommandId,
): RunResponseCommand {
    return [uint64(commandId), 5];
}

function declareTool(
    commandId: CommandId,
): DeclareToolCommand {
    return [uint64(commandId), 6];
}

function runTool(
    commandId: CommandId,
): RunToolCommand {
    return [uint64(commandId), 7];
}

function close(
    commandId: CommandId,
): CloseCommand {
    return [uint64(commandId), 8];
}

type CommandInput = {
    command_id: CommandId;
};

type RegisterInput = CommandInput & {
    config: SessionConfig;
};

function invoke(request: PluginRequest): SessionCommand {
    switch (request.api) {
        case Api.register: {
            const input = request.input as RegisterInput;
            return register(input.command_id, input.config);
        }
        case Api.declareRequest: {
            const input = request.input as CommandInput;
            return declareRequest(input.command_id);
        }
        case Api.runRequest: {
            const input = request.input as CommandInput;
            return runRequest(input.command_id);
        }
        case Api.declareResponse: {
            const input = request.input as CommandInput;
            return declareResponse(input.command_id);
        }
        case Api.runResponse: {
            const input = request.input as CommandInput;
            return runResponse(input.command_id);
        }
        case Api.declareTool: {
            const input = request.input as CommandInput;
            return declareTool(input.command_id);
        }
        case Api.runTool: {
            const input = request.input as CommandInput;
            return runTool(input.command_id);
        }
        case Api.close: {
            const input = request.input as CommandInput;
            return close(input.command_id);
        }
        default:
            throw new Error(
                "session-api does not support api: " + request.api,
            );
    }
}

export const plugin: Plugin = {
    invoke,
};
