import { isAbsolute, resolve } from "node:path";

import type {
  OpenAIFunctionToolCall,
  OpenAIToolResultMessage,
} from "./openai_executable_tool.js";

export interface ToolExecutionContext {
  repositoryPath: string;
  conversationId?: string;
  requestId?: string;
  signal?: AbortSignal;
}

export function requireArguments(
  toolCall: OpenAIFunctionToolCall,
  toolName: string,
): Record<string, unknown> {
  if (toolCall.type !== "function" || toolCall.function?.name !== toolName) {
    throw new Error(`Expected function tool ${toolName}.`);
  }
  const args = toolCall.function.arguments;
  if (!args || Array.isArray(args) || typeof args !== "object") {
    throw new Error("function.arguments phải là object.");
  }
  return args;
}

export function resultMessage(
  toolCall: OpenAIFunctionToolCall,
  result: unknown,
): OpenAIToolResultMessage {
  return {
    role: "tool",
    tool_call_id: toolCall.id,
    content: JSON.stringify(result),
  };
}

export function resolveToolPath(
  context: ToolExecutionContext,
  input?: unknown,
): string {
  if (typeof input !== "string" || !input.trim()) {
    return resolve(context.repositoryPath);
  }
  const value = input.trim();
  return isAbsolute(value) ? resolve(value) : resolve(context.repositoryPath, value);
}

export function requireString(value: unknown, label: string): string {
  if (typeof value !== "string" || !value.trim()) {
    throw new Error(`${label} is required`);
  }
  return value.trim();
}

export function readPositiveInteger(value: unknown, fallback: number): number {
  if (value === undefined || value === null) return fallback;
  const numeric = Number(value);
  if (!Number.isInteger(numeric) || numeric <= 0) {
    throw new Error("Expected a positive integer.");
  }
  return numeric;
}
