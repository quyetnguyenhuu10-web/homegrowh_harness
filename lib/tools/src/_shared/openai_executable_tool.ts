import { existsSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import {
  DEFAULT_TOOL_PROCESS_TIMEOUT_MS,
  processFailure,
  runProcess,
  type SandboxFilesystemAccess,
} from "../_runtime/process_runner.js";
import type { ToolExecutionContext } from "./native_tool.js";

export interface OpenAIFunctionToolCall {
  id: string;
  type: "function";
  function: {
    name: string;
    /** JSON struct trực tiếp: một object hoặc một mảng object. */
    arguments: Record<string, unknown> | Array<Record<string, unknown>>;
  };
}

export interface OpenAIToolResultMessage {
  role: "tool";
  tool_call_id: string;
  content: string;
}

export interface OpenAIFunctionToolDefinition {
  type: "function";
  function: {
    name: string;
    description: string;
    parameters: Record<string, unknown>;
  };
}

interface RunExecutableToolOptions {
  toolName: string;
  executableRelativePath: string;
  moduleUrl: string;
  toolCall: OpenAIFunctionToolCall;
  context: ToolExecutionContext;
  filesystemAccess: SandboxFilesystemAccess;
}

export function errorPayload(
  tool: string,
  callId: string,
  code: string,
  message: string,
  details: Record<string, unknown> = {},
): Record<string, unknown> {
  return {
    version: 1,
    tool,
    call_id: callId,
    ok: false,
    results: [],
    error: { code, message, ...details },
  };
}

export function toolResult(
  toolCallId: string,
  payload: unknown,
): OpenAIToolResultMessage {
  return {
    role: "tool",
    tool_call_id: toolCallId,
    content: JSON.stringify(payload),
  };
}

function executableWorkingDirectory(
  moduleUrl: string,
  toolName: string,
  executableRelativePath: string,
): string {
  const moduleDir = dirname(fileURLToPath(moduleUrl));
  if (existsSync(resolve(moduleDir, executableRelativePath))) {
    return moduleDir;
  }

  for (const start of [process.cwd(), moduleDir]) {
    let current = resolve(start);
    for (let depth = 0; depth < 10; depth += 1) {
      if (existsSync(resolve(current, executableRelativePath))) {
        return current;
      }

      const toolDir = join(current, "tools", toolName);
      if (existsSync(resolve(toolDir, executableRelativePath))) {
        return toolDir;
      }
      const parent = dirname(current);
      if (parent === current) break;
      current = parent;
    }
  }

  throw new Error(
    `Không tìm thấy executable tương đối ${executableRelativePath} cho ${toolName}.`,
  );
}

function parseExecutableOutput(stdout: string): unknown {
  const text = stdout.trim();
  if (!text) {
    throw new Error("Executable không trả JSON trên stdout.");
  }
  return JSON.parse(text) as unknown;
}

/**
 * Adapter OpenAI-compatible tool_call -> normalized executable envelope ->
 * OpenAI-compatible role=tool result.
 *
 * executableRelativePath được resolve bởi process thông qua cwd của chính module
 * tool, nên caller không phụ thuộc absolute path của checkout.
 */
export async function runOpenAIExecutableTool(
  options: RunExecutableToolOptions,
): Promise<OpenAIToolResultMessage> {
  const { toolCall, toolName, executableRelativePath } = options;

  if (!toolCall || typeof toolCall.id !== "string" || !toolCall.id) {
    throw new Error("OpenAI tool call cần id.");
  }

  if (toolCall.type !== "function") {
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "invalid_tool_call",
        `Tool call type phải là function, nhận ${String(toolCall.type)}.`,
      ),
    );
  }

  if (toolCall.function?.name !== toolName) {
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "wrong_tool",
        `Expected ${toolName}, received ${String(toolCall.function?.name)}.`,
      ),
    );
  }

  const rawArguments = toolCall.function.arguments;

  const requests = Array.isArray(rawArguments)
    ? rawArguments
    : [rawArguments];

  if (
    requests.length === 0 ||
    requests.some(
      (item) =>
        typeof item !== "object" ||
        item === null ||
        Array.isArray(item),
    )
  ) {
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "invalid_arguments",
        "function.arguments phải là object hoặc mảng object không rỗng.",
      ),
    );
  }

  let moduleDir: string;
  try {
    moduleDir = executableWorkingDirectory(
      options.moduleUrl,
      toolName,
      executableRelativePath,
    );
  } catch (error) {
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "executable_not_found",
        error instanceof Error ? error.message : String(error),
      ),
    );
  }
  const envelope = JSON.stringify({
    version: 1,
    tool: toolName,
    call_id: toolCall.id,
    arguments: { requests },
  });
  const executablePath = resolve(moduleDir, executableRelativePath);

  let processResult;
  try {
    processResult = await runProcess({
      executable: executablePath,
      args: ["--toolcall-stdin"],
      cwd: options.context.repositoryPath,
      stdin: envelope,
      timeoutMs: DEFAULT_TOOL_PROCESS_TIMEOUT_MS,
      refresh: false,
      sandbox: {
        filesystem: [
          { path: options.context.repositoryPath, access: options.filesystemAccess },
          { path: executablePath, access: "read_only" },
        ],
        network: "none",
      },
      signal: options.context.signal,
    });
  } catch (error) {
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "executable_start_failed",
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

  try {
    const payload = parseExecutableOutput(processResult.stdout);
    return toolResult(toolCall.id, payload);
  } catch (error) {
    const detail = processResult.stderr.trim();
    return toolResult(
      toolCall.id,
      errorPayload(
        toolName,
        toolCall.id,
        "invalid_executable_output",
        [
          `exit_code=${String(processResult.code)}`,
          error instanceof Error ? error.message : String(error),
          detail ? `stderr=${detail}` : "",
        ]
          .filter(Boolean)
          .join("; "),
      ),
    );
  }
}
