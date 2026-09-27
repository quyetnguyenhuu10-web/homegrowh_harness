import { realpathSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";

import {
  DEFAULT_TOOL_PROCESS_TIMEOUT_MS,
  processFailure,
  runProcess,
  type SandboxFilesystemAccess,
  type SandboxNetworkAccess,
} from "../_runtime/process_runner.js";
import { mergeReadFiles, readFilesFor } from "./read_state.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIToolResultMessage,
} from "./openai_executable_tool.js";
import { errorPayload, toolResult } from "./openai_executable_tool.js";
import type { ToolExecutionContext } from "./native_tool.js";

export interface TypeScriptToolHostRequest {
  version: 1;
  tool: string;
  toolCall: OpenAIFunctionToolCall;
  context: {
    repositoryPath: string;
    conversationId?: string;
    requestId?: string;
  };
  readFiles: string[];
}

export type TypeScriptToolHostResponse =
  | {
      ok: true;
      result: OpenAIToolResultMessage;
      readFiles: string[];
    }
  | {
      ok: false;
      error: TypeScriptToolHostError;
    };

export interface TypeScriptToolHostError {
  message: string;
  name?: string;
  code?: string;
  errno?: number;
  syscall?: string;
  path?: string;
  dest?: string;
}

interface RunOpenAITypeScriptToolOptions {
  toolName: string;
  toolCall: OpenAIFunctionToolCall;
  context: ToolExecutionContext;
  filesystemAccess?: SandboxFilesystemAccess;
  networkAccess?: SandboxNetworkAccess;
}

function toolHostPath(): string {
  return fileURLToPath(new URL("../_runtime/tool_host.js", import.meta.url));
}

function toolRuntimeRoot(): string {
  return resolve(dirname(toolHostPath()), "..");
}

function toolPackageJsonPath(): string {
  return resolve(toolRuntimeRoot(), "../..", "package.json");
}

function childEnvironment(): NodeJS.ProcessEnv {
  if (!("electron" in process.versions)) return process.env;
  return { ...process.env, ELECTRON_RUN_AS_NODE: "1" };
}

/**
 * Runs one TypeScript tool in a dedicated Node process.
 *
 * The parent keeps durable/runtime state; the child receives a snapshot and
 * returns the updated read-state snapshot with its result. The child process
 * is the only place where the tool implementation module is imported.
 */
export async function runOpenAITypeScriptTool(
  options: RunOpenAITypeScriptToolOptions,
): Promise<OpenAIToolResultMessage> {
  const { toolCall, toolName, context } = options;
  const request: TypeScriptToolHostRequest = {
    version: 1,
    tool: toolName,
    toolCall,
    context: {
      repositoryPath: context.repositoryPath,
      ...(context.conversationId === undefined
        ? {}
        : { conversationId: context.conversationId }),
      ...(context.requestId === undefined ? {} : { requestId: context.requestId }),
    },
    readFiles: readFilesFor(context.conversationId, context.requestId),
  };

  const runtimeRoot = toolRuntimeRoot();
  const runtimeExecutable = realpathSync.native(process.execPath);
  const filesystem = [
    { path: runtimeRoot, access: "read_only" as const },
    { path: toolPackageJsonPath(), access: "read_only" as const },
    { path: dirname(runtimeExecutable), access: "read_only" as const },
    ...(options.filesystemAccess === undefined
      ? []
      : [{ path: context.repositoryPath, access: options.filesystemAccess }]),
  ];

  let processResult;
  try {
    processResult = await runProcess({
      executable: runtimeExecutable,
      args: [toolHostPath()],
      cwd: options.filesystemAccess === undefined ? runtimeRoot : context.repositoryPath,
      stdin: JSON.stringify(request),
      env: childEnvironment(),
      signal: context.signal,
      timeoutMs: DEFAULT_TOOL_PROCESS_TIMEOUT_MS,
      refresh: false,
      sandbox: {
        filesystem,
        network: options.networkAccess ?? null,
      },
    });
  } catch (error) {
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "sandbox_start_failed",
        error instanceof Error ? error.message : String(error),
      ),
    );
  }

  const failure = processFailure(processResult);
  if (failure) {
    return toolResult(
      toolCall.id,
      errorPayload(toolName, toolCall.id, failure.code, failure.message, {
        os_error: failure.osError,
        path_errors: failure.pathErrors,
      }),
    );
  }

  const stdout = processResult.stdout.trim();
  if (!stdout) {
    throw new Error(
      `TypeScript tool host returned no JSON (exit=${String(processResult.code)}${
        processResult.stderr.trim() ? `; stderr=${processResult.stderr.trim()}` : ""
      })`,
    );
  }

  let response: TypeScriptToolHostResponse;
  try {
    response = JSON.parse(stdout) as TypeScriptToolHostResponse;
  } catch (error) {
    throw new Error(
      `TypeScript tool host returned invalid JSON: ${
        error instanceof Error ? error.message : String(error)
      }`,
    );
  }

  if (!response.ok) {
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "tool_execution_error",
        response.error.message,
        { source_error: response.error },
      ),
    );
  }

  mergeReadFiles(
    response.readFiles,
    context.conversationId,
    context.requestId,
  );
  return response.result;
}
