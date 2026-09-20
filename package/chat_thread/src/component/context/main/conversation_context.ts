import type {
  ChatMessage,
  ChatToolCall,
  ChatToolDefinition,
} from "@homegrowh/provider";

import { toolRegistry } from "../../../../../tools/registry/index";

import {
  DEFAULT_REASONING_HISTORY_POLICY,
  readConversation,
  selectReasoningRequestIds,
  type AddRepositoryOptions,
  type HistoryRow,
  type ReasoningHistoryPolicy,
} from "../../history_conversation/main";
import { loadSystemPromptMessages } from "./system_prompt";

export interface ModelContext {
  messages: ChatMessage[];
  tools: ChatToolDefinition[];
}

function parseToolResult(row: HistoryRow): ChatMessage {
  if (row.content === null) {
    throw new Error(`History toolresult row ${row.id} thiếu content.`);
  }
  const parsed = JSON.parse(row.content) as ChatMessage;
  if (parsed.role !== "tool" || typeof parsed.tool_call_id !== "string") {
    throw new Error(`History toolresult row ${row.id} không hợp lệ.`);
  }
  return parsed;
}

function parseLegacyToolCall(row: HistoryRow): ChatMessage {
  if (row.content === null) {
    throw new Error(`History legacy toolcall row ${row.id} thiếu content.`);
  }
  const parsed = JSON.parse(row.content) as ChatMessage;
  if (parsed.role !== "assistant" || !Array.isArray(parsed.tool_calls)) {
    throw new Error(`History legacy toolcall row ${row.id} không hợp lệ.`);
  }
  return parsed;
}

function requireDelta(row: HistoryRow): string {
  if (row.delta === null) {
    throw new Error(`History ${row.type} row ${row.id} thiếu delta.`);
  }
  return row.delta;
}

type DeltaRowType = "reasoning" | "reply" | "toolcall";

interface DeltaGroup {
  kind: "delta";
  type: DeltaRowType;
  apiSession: string;
  requestId: string | null;
  eventIndex: number;
  firstRowId: number;
  delta: string;
}

interface FullRowEvent {
  kind: "row";
  row: HistoryRow;
}

type ConversationEvent = DeltaGroup | FullRowEvent;

function isDeltaRowType(type: string): type is DeltaRowType {
  return type === "reasoning" || type === "reply" || type === "toolcall";
}

function groupConversationEvents(rows: readonly HistoryRow[]): ConversationEvent[] {
  const events: ConversationEvent[] = [];
  const groups = new Map<string, DeltaGroup>();
  let legacyGroup = 0;
  let previousLegacyKey: string | null = null;

  for (const row of rows) {
    if (!isDeltaRowType(row.type) || row.delta === null) {
      previousLegacyKey = null;
      events.push({ kind: "row", row });
      continue;
    }

    const apiSession = row.API_sessions;
    if (apiSession === null) {
      throw new Error(`History delta row ${row.id} thiếu API_sessions.`);
    }

    let key: string;
    let eventIndex: number;
    if (row.event_index !== null) {
      key = `${apiSession}\u0000${row.event_index}`;
      eventIndex = row.event_index;
      previousLegacyKey = null;
    } else {
      const candidate = `${apiSession}\u0000${row.type}`;
      if (previousLegacyKey !== candidate) {
        legacyGroup += 1;
        previousLegacyKey = candidate;
      }
      key = `legacy\u0000${legacyGroup}`;
      eventIndex = legacyGroup;
    }

    const existing = groups.get(key);
    if (existing) {
      if (
        existing.type !== row.type ||
        existing.apiSession !== apiSession ||
        existing.requestId !== row.request_id
      ) {
        throw new Error(
          `History event_index ${eventIndex} trong API session ${apiSession} trộn nhiều type.`,
        );
      }
      existing.delta += requireDelta(row);
      continue;
    }

    const group: DeltaGroup = {
      kind: "delta",
      type: row.type,
      apiSession,
      requestId: row.request_id,
      eventIndex,
      firstRowId: row.id,
      delta: requireDelta(row),
    };
    groups.set(key, group);
    events.push(group);
  }

  return events;
}

function parseToolCall(group: DeltaGroup): ChatToolCall | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(group.delta);
  } catch {
    return null;
  }

  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
    return null;
  }
  const call = parsed as Record<string, unknown>;
  const fn = call.function;
  if (
    typeof call.id !== "string" ||
    call.type !== "function" ||
    typeof fn !== "object" ||
    fn === null ||
    Array.isArray(fn) ||
    typeof (fn as Record<string, unknown>).name !== "string"
  ) {
    return null;
  }
  return parsed as ChatToolCall;
}

/**
 * Fold tuần tự theo id. Delta được ghép bằng khóa
 * `API_sessions + event_index`; thứ tự logical event là lần xuất hiện đầu tiên
 * của khóa đó trong event log.
 */
export function historyRowsToOpenAIMessages(
  rows: readonly HistoryRow[],
  reasoningHistory: ReasoningHistoryPolicy = DEFAULT_REASONING_HISTORY_POLICY,
): ChatMessage[] {
  const messages: ChatMessage[] = [];
  const reasoningRequestIds = selectReasoningRequestIds(rows, reasoningHistory);
  let pendingApiSession: string | null = null;
  let pendingReasoning = "";
  let pendingReply = "";
  let pendingToolCalls: ChatToolCall[] = [];

  const flushAssistant = (): void => {
    if (pendingReply || pendingToolCalls.length > 0) {
      messages.push({
        role: "assistant",
        content: pendingReply || null,
        ...(pendingReasoning ? { reasoning_content: pendingReasoning } : {}),
        ...(pendingToolCalls.length > 0
          ? { tool_calls: pendingToolCalls }
          : {}),
      });
    }
    pendingApiSession = null;
    pendingReasoning = "";
    pendingReply = "";
    pendingToolCalls = [];
  };

  const enterApiSession = (apiSession: string): void => {
    if (pendingApiSession !== null && pendingApiSession !== apiSession) {
      flushAssistant();
    }
    pendingApiSession ??= apiSession;
  };

  for (const event of groupConversationEvents(rows)) {
    if (event.kind === "delta") {
      enterApiSession(event.apiSession);
      if (
        event.type === "reasoning" &&
        event.requestId !== null &&
        reasoningRequestIds.has(event.requestId)
      ) {
        pendingReasoning += event.delta;
      } else if (event.type === "reply") {
        pendingReply += event.delta;
      } else if (event.type === "toolcall") {
        const toolCall = parseToolCall(event);
        if (toolCall) pendingToolCalls.push(toolCall);
      }
      continue;
    }

    const row = event.row;
    if (row.type === "user") {
      flushAssistant();
      if (row.content === null) {
        throw new Error(`History user row ${row.id} thiếu content.`);
      }
      messages.push({ role: "user", content: row.content });
      continue;
    }

    if (row.type === "toolresult" || row.type === "toolresults") {
      flushAssistant();
      messages.push(parseToolResult(row));
      continue;
    }

    // Tương thích dữ liệu cũ đã được migrate sang schema mới.
    if (row.type === "toolcall" && row.content !== null) {
      flushAssistant();
      messages.push(parseLegacyToolCall(row));
      continue;
    }
    if (row.type === "message" && row.content !== null) {
      flushAssistant();
      if (row.role === "user" || row.role === "assistant" || row.role === "system") {
        messages.push({ role: row.role, content: row.content });
      }
    }
  }

  flushAssistant();

  return messages;
}

/**
 * Đọc toàn bộ conversation từ DB và dựng context gửi provider.
 * readConversation() hiện đọc full DB theo thứ tự cũ -> mới, không cắt context.
 */
export function buildConversationContext(
  repositoryPath: string,
  conversationId: string,
  options: AddRepositoryOptions = {},
  reasoningHistory: ReasoningHistoryPolicy = DEFAULT_REASONING_HISTORY_POLICY,
): ChatMessage[] {
  return [
    ...loadSystemPromptMessages({
      activeRepositoryPath: repositoryPath,
    }),
    ...historyRowsToOpenAIMessages(
      readConversation(repositoryPath, conversationId, options),
      reasoningHistory,
    ),
  ];
}

export function buildModelContext(
  repositoryPath: string,
  conversationId: string,
  options: AddRepositoryOptions = {},
  reasoningHistory: ReasoningHistoryPolicy = DEFAULT_REASONING_HISTORY_POLICY,
): ModelContext {
  return {
    messages: buildConversationContext(
      repositoryPath,
      conversationId,
      options,
      reasoningHistory,
    ),
    tools: toolRegistry.definitions() satisfies ChatToolDefinition[],
  };
}
