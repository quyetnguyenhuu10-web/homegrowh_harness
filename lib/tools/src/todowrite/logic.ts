import type {
  OpenAIFunctionToolCall,
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

export const TODOWRITE_TOOL_NAME = "todowrite";

export async function executeTodowriteToolCall(
  toolCall: OpenAIFunctionToolCall,
  _context: ToolExecutionContext,
): Promise<OpenAIToolResultMessage> {
  const args = requireArguments(toolCall, TODOWRITE_TOOL_NAME);
  if (!Array.isArray(args.todos)) throw new Error("todos is required");

  const next: TodoItem[] = args.todos.map((todo) => {
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

  return resultMessage(toolCall, {
    title: `${next.filter((todo) => todo.status !== "completed").length} todos`,
    output: JSON.stringify(next, null, 2),
    metadata: { todos: next },
  });
}
