import { toolDefinition } from "../tool_definitions.js";
import {
  runOpenAIExecutableTool,
  type OpenAIFunctionToolCall,
  type OpenAIFunctionToolDefinition,
  type OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

export const EDIT_FILE_TOOL_NAME = "edit_file";

/** Relative từ package/tools. Không hardcode checkout path. */
export const EDIT_FILE_EXECUTABLE =
  "filesystems/build/Debug/edit_file.exe";

export const editFileToolDefinition = toolDefinition(EDIT_FILE_TOOL_NAME);

export function executeEditFileToolCall(
  toolCall: OpenAIFunctionToolCall,
  _context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  return runOpenAIExecutableTool({
    toolName: EDIT_FILE_TOOL_NAME,
    executableRelativePath: EDIT_FILE_EXECUTABLE,
    moduleUrl: import.meta.url,
    toolCall,
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
