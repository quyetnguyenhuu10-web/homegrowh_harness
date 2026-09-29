import { toolDefinition } from "../tool_definitions.js";
import { runOpenAITypeScriptTool } from "../_shared/openai_typescript_tool.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

export const READ_FILE_TOOL_NAME = "read";
export const readFileToolDefinition = toolDefinition(READ_FILE_TOOL_NAME);

export function executeReadFileToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  return runOpenAITypeScriptTool({
    toolName: READ_FILE_TOOL_NAME,
    toolCall,
    context,
    filesystemAccess: "read_only",
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
};
