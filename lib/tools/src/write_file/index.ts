import { toolDefinition } from "../tool_definitions.js";
import { runOpenAITypeScriptTool } from "../_shared/openai_typescript_tool.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

export const WRITE_FILE_TOOL_NAME = "write";
export const writeFileToolDefinition = toolDefinition(WRITE_FILE_TOOL_NAME);

export function executeWriteFileToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  return runOpenAITypeScriptTool({
    toolName: WRITE_FILE_TOOL_NAME,
    toolCall,
    context,
    filesystemAccess: "read_write",
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
};
