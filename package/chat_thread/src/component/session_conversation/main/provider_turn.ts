import {
  callProvider,
  type ChatMessage,
  type ChatToolCall,
  type ChatToolDefinition,
  type JsonArguments,
  type ProviderName,
} from "@homegrowh/provider";

import type { SessionModelEvent } from "./model_events";

interface StreamingToolCallBuffer {
  eventIndex: number;
  id: string;
  name: string;
  argumentText: string;
  structuredArguments?: JsonArguments;
  issues: string[];
}

export interface ProviderToolCallError {
  code: "invalid_tool_call_schema";
  message: string;
}

export interface ProviderToolCallAttempt {
  toolCall: ChatToolCall;
  error?: ProviderToolCallError;
}

export interface ProviderTurnInput {
  requestId: string;
  apiSession: string;
  provider: ProviderName;
  model: string;
  apiKey?: string;
  baseUrl?: string;
  messages: ChatMessage[];
  tools?: ChatToolDefinition[];
  signal: AbortSignal;
  onEvent(event: SessionModelEvent): void | Promise<void>;
}

export interface ProviderTurnResult {
  toolCalls: ProviderToolCallAttempt[];
}

function isJsonArguments(value: unknown): value is JsonArguments {
  if (typeof value !== "object" || value === null) return false;
  if (!Array.isArray(value)) return true;
  return (
    value.length > 0 &&
    value.every(
      (item) =>
        typeof item === "object" && item !== null && !Array.isArray(item),
    )
  );
}

function collectStreamingToolCalls(
  raw: unknown,
  buffers: Map<number, StreamingToolCallBuffer>,
  allocateEventIndex: () => number,
): void {
  if (typeof raw !== "object" || raw === null) return;
  const choices = (raw as Record<string, unknown>).choices;
  if (!Array.isArray(choices)) return;
  const first = choices[0];
  if (typeof first !== "object" || first === null) return;
  const delta = (first as Record<string, unknown>).delta;
  if (typeof delta !== "object" || delta === null) return;
  const deltaObject = delta as Record<string, unknown>;
  if (!("tool_calls" in deltaObject)) return;
  const rawCalls = deltaObject.tool_calls;
  if (!Array.isArray(rawCalls)) {
    const current = buffers.get(0) ?? {
      eventIndex: allocateEventIndex(),
      id: "",
      name: "",
      argumentText: "",
      issues: [],
    };
    current.issues.push("tool_calls không phải array.");
    buffers.set(0, current);
    return;
  }

  rawCalls.forEach((rawCall, fallbackIndex) => {
    if (typeof rawCall !== "object" || rawCall === null) {
      const current = buffers.get(fallbackIndex) ?? {
        eventIndex: allocateEventIndex(),
        id: "",
        name: "",
        argumentText: "",
        issues: [],
      };
      current.issues.push(`tool_calls[${fallbackIndex}] không phải object.`);
      buffers.set(fallbackIndex, current);
      return;
    }
    const call = rawCall as Record<string, unknown>;
    const providerIndex =
      typeof call.index === "number" && Number.isInteger(call.index)
        ? call.index
        : fallbackIndex;
    const current = buffers.get(providerIndex) ?? {
      eventIndex: allocateEventIndex(),
      id: "",
      name: "",
      argumentText: "",
      issues: [],
    };

    if (typeof call.id === "string") {
      current.id = call.id;
    } else if (call.id !== undefined) {
      current.issues.push(`tool_calls[${providerIndex}].id không phải string.`);
    }
    if (call.type !== undefined && call.type !== "function") {
      current.issues.push(`tool_calls[${providerIndex}].type không phải "function".`);
    }
    const fn = call.function;
    if (typeof fn === "object" && fn !== null) {
      const functionObject = fn as Record<string, unknown>;
      if (typeof functionObject.name === "string") {
        current.name = functionObject.name;
      } else if (functionObject.name !== undefined) {
        current.issues.push(
          `tool_calls[${providerIndex}].function.name không phải string.`,
        );
      }
      if (typeof functionObject.arguments === "string") {
        current.argumentText += functionObject.arguments;
      } else if (isJsonArguments(functionObject.arguments)) {
        current.structuredArguments = functionObject.arguments;
      } else if (functionObject.arguments !== undefined) {
        current.issues.push(
          `tool_calls[${providerIndex}].function.arguments không phải JSON object/array-object hoặc JSON string.`,
        );
      }
    } else if (fn !== undefined) {
      current.issues.push(`tool_calls[${providerIndex}].function không phải object.`);
    }

    buffers.set(providerIndex, current);
  });
}

function finishStreamingToolCalls(
  buffers: Map<number, StreamingToolCallBuffer>,
  apiSession: string,
): Array<ProviderToolCallAttempt & { eventIndex: number }> {
  return [...buffers.entries()]
    .sort(([left], [right]) => left - right)
    .map(([index, buffer]) => {
      const issues = [...buffer.issues];
      let args: unknown = buffer.structuredArguments;
      if (args === undefined) {
        const text = buffer.argumentText.trim();
        try {
          args = text ? JSON.parse(text) : {};
        } catch (error) {
          issues.push(
            `tool_calls[${index}] arguments JSON không hợp lệ: ${
              error instanceof Error ? error.message : String(error)
            }`,
          );
          args = {};
        }
      }
      if (!buffer.id.trim()) {
        issues.push(`tool_calls[${index}] thiếu id.`);
      }
      if (!buffer.name.trim()) {
        issues.push(`tool_calls[${index}] thiếu function.name.`);
      }
      let normalizedArguments: JsonArguments;
      if (isJsonArguments(args)) {
        normalizedArguments = args;
      } else {
        issues.push(`tool_calls[${index}] arguments không phải object/array-object.`);
        normalizedArguments = {};
      }

      const safeId =
        buffer.id.trim() ||
        `invalid_${apiSession.replace(/[^A-Za-z0-9_-]/g, "_")}_${index}`;
      const safeName = buffer.name.trim() || "invalid_tool_call";
      const safeArguments: JsonArguments =
        issues.length === 0
          ? normalizedArguments
          : {
              __invalid_tool_call__: true,
              raw_arguments:
                buffer.structuredArguments !== undefined
                  ? buffer.structuredArguments
                  : buffer.argumentText,
              issues,
            };
      const toolCall: ChatToolCall = {
        id: safeId,
        type: "function" as const,
        function: {
          name: safeName,
          arguments: safeArguments,
        },
      };

      return {
        eventIndex: buffer.eventIndex,
        toolCall,
        ...(issues.length > 0
          ? {
              error: {
                code: "invalid_tool_call_schema" as const,
                message: issues.join(" "),
              },
            }
          : {}),
      };
    });
}

export async function runProviderTurn(
  input: ProviderTurnInput,
): Promise<ProviderTurnResult> {
  const toolCallBuffers = new Map<number, StreamingToolCallBuffer>();
  let nextEventIndex = 1;
  let reasoningEventIndex: number | null = null;
  let replyEventIndex: number | null = null;
  const allocateEventIndex = (): number => nextEventIndex++;

  for await (const chunk of callProvider({
    provider: input.provider,
    model: input.model,
    apiKey: input.apiKey,
    baseUrl: input.baseUrl,
    messages: input.messages,
    tools: input.tools,
    signal: input.signal,
  })) {
    if (chunk.done) break;

    if (chunk.reasoningDelta) {
      reasoningEventIndex ??= allocateEventIndex();
      await input.onEvent({
        type: "assistant.reasoning.delta",
        requestId: input.requestId,
        apiSession: input.apiSession,
        eventIndex: reasoningEventIndex,
        delta: chunk.reasoningDelta,
      });
    }

    collectStreamingToolCalls(chunk.raw, toolCallBuffers, allocateEventIndex);

    if (chunk.delta) {
      replyEventIndex ??= allocateEventIndex();
      await input.onEvent({
        type: "assistant.delta",
        requestId: input.requestId,
        apiSession: input.apiSession,
        eventIndex: replyEventIndex,
        delta: chunk.delta,
      });
    }
  }

  if (toolCallBuffers.size === 0) {
    return { toolCalls: [] };
  }

  const attempts = finishStreamingToolCalls(toolCallBuffers, input.apiSession);
  for (const attempt of attempts) {
    await input.onEvent({
      type: "assistant.tool_call.delta",
      requestId: input.requestId,
      apiSession: input.apiSession,
      eventIndex: attempt.eventIndex,
      delta: JSON.stringify(attempt.toolCall),
    });
  }

  return {
    toolCalls: attempts.map(({ eventIndex: _drop, ...attempt }) => attempt),
  };
}
