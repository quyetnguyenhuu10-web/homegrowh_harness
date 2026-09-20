import type { HistoryRow } from "../../history_conversation/renderer";

import type { HistoryEntry } from "./ViewHistory";

type DeltaEntryType = "reasoning" | "reply" | "toolcall";

function isDeltaEntryType(type: string): type is DeltaEntryType {
  return type === "reasoning" || type === "reply" || type === "toolcall";
}

function normalizedType(type: string): string {
  return type.trim().toLowerCase().replace(/[\s_-]+/g, "");
}

function isThinkingEntry(entry: HistoryEntry): boolean {
  const type = normalizedType(entry.type);
  return type === "thinking" || type === "reasoning";
}

function isAssistantAnswerEntry(entry: HistoryEntry): boolean {
  const type = normalizedType(entry.type);
  if (type === "reply") return true;
  return type === "message" && entry.role === "assistant";
}

function markLogicalSpacing(entries: HistoryEntry[]): HistoryEntry[] {
  for (let index = 0; index + 1 < entries.length; index += 1) {
    const current = entries[index];
    const next = entries[index + 1];
    if (isThinkingEntry(current) && isAssistantAnswerEntry(next)) {
      current.compactAfter = true;
    }
  }
  return entries;
}

function toolResultText(content: string): string {
  try {
    const parsed = JSON.parse(content) as unknown;
    if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
      return content;
    }
    const value = (parsed as Record<string, unknown>).content;
    return typeof value === "string" ? value : content;
  } catch {
    return content;
  }
}

/**
 * Project DB rows -> logical UI entries.
 *
 * Delta rows with the same API_sessions + event_index form exactly one entry.
 * Their delta text is concatenated by DB id order. User/toolresult remain one
 * full-content entry per DB row.
 */
export function projectHistoryEntries(
  conversationId: string,
  rows: readonly HistoryRow[],
): HistoryEntry[] {
  const entries: HistoryEntry[] = [];
  const deltaEntries = new Map<string, HistoryEntry>();

  for (const row of [...rows].sort((left, right) => left.id - right.id)) {
    if (row.type === "error") continue;

    if (
      isDeltaEntryType(row.type) &&
      row.delta !== null &&
      row.API_sessions !== null &&
      row.event_index !== null
    ) {
      const key = `${row.API_sessions}\u0000${row.event_index}`;
      const existing = deltaEntries.get(key);
      if (existing) {
        existing.text = `${existing.text ?? ""}${row.delta}`;
        existing.sourceRowIds.push(row.id);
        continue;
      }

      const entry: HistoryEntry = {
        id: `${conversationId}:entry:${row.API_sessions}:${row.event_index}`,
        type: row.type,
        role: row.role,
        text: row.delta,
        apiSession: row.API_sessions,
        eventIndex: row.event_index,
        sourceRowIds: [row.id],
      };
      deltaEntries.set(key, entry);
      entries.push(entry);
      continue;
    }

    if (row.type === "user") {
      entries.push({
        id: `${conversationId}:entry:row:${row.id}`,
        type: "user",
        role: "user",
        text: row.content ?? "",
        apiSession: row.API_sessions,
        eventIndex: null,
        sourceRowIds: [row.id],
      });
      continue;
    }

    if (row.type === "toolresult" || row.type === "toolresults") {
      entries.push({
        id: `${conversationId}:entry:row:${row.id}`,
        type: "toolresult",
        role: "tool",
        text: row.content === null ? "" : toolResultText(row.content),
        apiSession: row.API_sessions,
        eventIndex: null,
        sourceRowIds: [row.id],
      });
      continue;
    }

    // Legacy/fallback row: still show it as one logical entry instead of losing it.
    entries.push({
      id: `${conversationId}:entry:row:${row.id}`,
      type: row.type,
      role: row.role,
      text: row.content ?? row.delta ?? "",
      apiSession: row.API_sessions,
      eventIndex: row.event_index,
      sourceRowIds: [row.id],
    });
  }

  return markLogicalSpacing(entries);
}
