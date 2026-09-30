import type { Plugin, PluginRequest } from "@hh/plugin-loader";
import {
    closeSession, declareRequest, declareResponse, declareTool, registerSession,
    runRequest, runResponse, runTool, type CommandId, type SessionCommand,
    type SessionConfig, type UInt64,
} from "./commands.js";
import { failure, makeError, normalizeError, success, type HHError, type Result } from "./error.js";
import { parseEvent } from "./events.js";
import {
    closeRuntime, executeRuntimeCommand, openRuntime, receiveRuntime, sendRuntime,
    streamRuntimeError, streamRuntimeEvent,
} from "./runtime.js";

export type { HHError, Result } from "./error.js";

const Api = {
    register: 1, declareRequest: 2, runRequest: 3, declareResponse: 4,
    runResponse: 5, declareTool: 6, runTool: 7, close: 8, openRuntime: 9,
    send: 10, receive: 11, closeRuntime: 12, parseEvent: 13,
    streamEvent: 14, getError: 15, streamError: 16,
} as const;

const errors: HHError[] = [];

type CommandInput = { runtime_id: UInt64; command_id: CommandId };
type RegisterInput = CommandInput & { config: SessionConfig };
type RuntimeInput = { runtime_id: UInt64 };
type SendInput = RuntimeInput & { message: unknown };
type ParseEventInput = { raw: unknown };
type OpenRuntimeInput = { executable: string };

function inputObject(request: PluginRequest): Result<Record<string, unknown>> {
    if (typeof request.input !== "object" || request.input === null || Array.isArray(request.input)) {
        return failure(makeError("invoke", "validation_error", "API input must be an object",
            [{ api_id: request.api, input: request.input }]));
    }
    return success(request.input as Record<string, unknown>);
}

async function execute(input: CommandInput, prepared: Result<SessionCommand>): Promise<Result<unknown>> {
    return prepared.error === null
        ? executeRuntimeCommand(input.runtime_id, input.command_id, prepared.value)
        : failure(prepared.error);
}

async function invokeApi(request: PluginRequest): Promise<Result<unknown>> {
    if (!Object.values(Api).includes(request.api as typeof Api[keyof typeof Api])) {
        return failure(makeError("invoke", "validation_error", "Session API is unsupported", [{ api_id: request.api }]));
    }
    const parsedInput = inputObject(request);
    if (parsedInput.error !== null) return failure(parsedInput.error);
    const input = parsedInput.value;
    switch (request.api) {
        case Api.register: {
            const command = input as RegisterInput;
            return execute(command, registerSession(command.command_id, command.config));
        }
        case Api.declareRequest: {
            const command = input as CommandInput;
            return execute(command, declareRequest(command.command_id));
        }
        case Api.runRequest: {
            const command = input as CommandInput;
            return execute(command, runRequest(command.command_id));
        }
        case Api.declareResponse: {
            const command = input as CommandInput;
            return execute(command, declareResponse(command.command_id));
        }
        case Api.runResponse: {
            const command = input as CommandInput;
            return execute(command, runResponse(command.command_id));
        }
        case Api.declareTool: {
            const command = input as CommandInput;
            return execute(command, declareTool(command.command_id));
        }
        case Api.runTool: {
            const command = input as CommandInput;
            return execute(command, runTool(command.command_id));
        }
        case Api.close: {
            const command = input as CommandInput;
            return execute(command, closeSession(command.command_id));
        }
        case Api.openRuntime:
            return openRuntime((input as OpenRuntimeInput).executable);
        case Api.send: {
            const send = input as SendInput;
            return sendRuntime(send.runtime_id, send.message);
        }
        case Api.receive:
            return receiveRuntime((input as RuntimeInput).runtime_id);
        case Api.closeRuntime:
            return closeRuntime((input as RuntimeInput).runtime_id);
        case Api.parseEvent:
            return parseEvent((input as ParseEventInput).raw);
        case Api.streamEvent:
            return streamRuntimeEvent((input as RuntimeInput).runtime_id);
        case Api.streamError:
            return streamRuntimeError((input as RuntimeInput).runtime_id);
        default:
            return failure(makeError("invoke", "validation_error", "Session API is unsupported", [{ api_id: request.api }]));
    }
}

async function invoke(request: PluginRequest): Promise<Result<unknown> | HHError[]> {
    let result: Result<unknown>;
    try {
        if (request.api === Api.getError) return errors.splice(0);
        result = await invokeApi(request);
    } catch (error) {
        result = failure(normalizeError(error, "invoke", "validation_error"));
    }
    if (result.error !== null) errors.push(result.error);
    return result;
}

export const plugin = { invoke } satisfies Plugin;
