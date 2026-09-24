import { toolDefinition } from "../tool_definitions.js";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool.js";
import {
  requireArguments,
  requireString,
  resultMessage,
  type ToolExecutionContext,
} from "../_shared/native_tool.js";

interface TodoItem {
  content: string;
  status: string;
  priority: string;
}

const todosByConversation = new Map<string, TodoItem[]>();

export const TODOWRITE_TOOL_NAME = "todowrite";

export const todowriteToolDefinition = toolDefinition(TODOWRITE_TOOL_NAME);

export async function executeTodowriteToolCall(
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  const args = requireArguments(toolCall, TODOWRITE_TOOL_NAME);
  if (!Array.isArray(args.todos)) throw new Error("todos is required");

  const next = args.todos.map((todo) => {
    if (!todo || typeof todo !== "object" || Array.isArray(todo)) {
      throw new Error("Each todo must be an object.");
    }
    const record = todo as Record<string, unknown>;
    return {
      content: requireString(record.content, "todo.content"),
      status: requireString(record.status, "todo.status"),
      priority: requireString(record.priority, "todo.priority"),
    };
  });

  const key = context.conversationId ?? context.requestId ?? "default";
  todosByConversation.set(key, next);

  return resultMessage(toolCall, {
    title: `${next.filter((todo) => todo.status !== "completed").length} todos`,
    output: JSON.stringify(next, null, 2),
    metadata: { todos: next },
  });
}
