import {
  executeEditFileToolCall,
  editFileToolDefinition,
} from "./edit_file/index.js";
import {
  executeGlobToolCall,
  globToolDefinition,
} from "./glob/index.js";
import {
  executeGrepToolCall,
  grepToolDefinition,
} from "./grep/index.js";
import {
  executeReadFileToolCall,
  readFileToolDefinition,
} from "./read_file/index.js";
import {
  executeTodowriteToolCall,
  todowriteToolDefinition,
} from "./todowrite/index.js";
import {
  executeWebfetchToolCall,
  webfetchToolDefinition,
} from "./webfetch/index.js";
import {
  executeWriteFileToolCall,
  writeFileToolDefinition,
} from "./write_file/index.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "./_shared/openai_executable_tool.js";
import type { ToolExecutionContext } from "./_shared/native_tool.js";

export type PublicTool = ((
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
) => Promise<OpenAIToolResultMessage>) & {
  readonly definition: OpenAIFunctionToolDefinition;
};

function publicTool(
  execute: (
    toolCall: OpenAIFunctionToolCall,
    context: ToolExecutionContext,
  ) => Promise<OpenAIToolResultMessage>,
  definition: OpenAIFunctionToolDefinition,
): PublicTool {
  return Object.assign(execute, {
    definition: structuredClone(definition),
  });
}

export const read = publicTool(executeReadFileToolCall, readFileToolDefinition);
export const write = publicTool(executeWriteFileToolCall, writeFileToolDefinition);
export const edit_file = publicTool(executeEditFileToolCall, editFileToolDefinition);
export const glob = publicTool(executeGlobToolCall, globToolDefinition);
export const grep = publicTool(executeGrepToolCall, grepToolDefinition);
export const webfetch = publicTool(executeWebfetchToolCall, webfetchToolDefinition);
export const todowrite = publicTool(executeTodowriteToolCall, todowriteToolDefinition);
