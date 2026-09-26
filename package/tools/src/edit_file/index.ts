import { toolDefinition } from "../tool_definitions.js";
import {
  runOpenAIExecutableTool,
  type OpenAIFunctionToolCall,
  type OpenAIFunctionToolDefinition,
  type OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

export const EDIT_FILE_TOOL_NAME = "edit_file";

/** Stable runtime artifact. Resolution walks upward to the repository root. */
export const EDIT_FILE_EXECUTABLE =
  process.platform === "win32"
    ? "executable/edit_file.exe"
    : "executable/edit_file";

export const editFileToolDefinition = toolDefinition(EDIT_FILE_TOOL_NAME);

export function executeEditFileToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  return runOpenAIExecutableTool({
    toolName: EDIT_FILE_TOOL_NAME,
    executableRelativePath: EDIT_FILE_EXECUTABLE,
    moduleUrl: import.meta.url,
    toolCall,
    context,
    filesystemAccess: "read_write",
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
