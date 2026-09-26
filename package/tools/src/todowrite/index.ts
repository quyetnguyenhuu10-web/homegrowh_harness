import { toolDefinition } from "../tool_definitions.js";
import { runOpenAITypeScriptTool } from "../_shared/openai_typescript_tool.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

export const TODOWRITE_TOOL_NAME = "todowrite";

export const todowriteToolDefinition = toolDefinition(TODOWRITE_TOOL_NAME);

export function executeTodowriteToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  return runOpenAITypeScriptTool({
    toolName: TODOWRITE_TOOL_NAME,
    toolCall,
    context,
  });
}
