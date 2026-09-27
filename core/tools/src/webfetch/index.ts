import { toolDefinition } from "../tool_definitions.js";
import { runOpenAITypeScriptTool } from "../_shared/openai_typescript_tool.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

export const WEBFETCH_TOOL_NAME = "webfetch";
export const webfetchToolDefinition = toolDefinition(WEBFETCH_TOOL_NAME);

export function executeWebfetchToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  return runOpenAITypeScriptTool({
    toolName: WEBFETCH_TOOL_NAME,
    toolCall,
    context,
    networkAccess: {
      name: "internet_client",
      windowsSid: "S-1-15-3-1",
    },
  });
}

export type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
};
