import { compareHistoryRows } from "../../history_conversation/renderer/history_rows";
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
 * Delta rows with the same requestId + eventIndex form exactly one entry.
 * Their delta text is concatenated by Database logical order. User/toolresult remain one
 * full-content entry per DB row.
 */
export function projectHistoryEntries(
  conversationId: string,
  rows: readonly HistoryRow[],
): HistoryEntry[] {
  const entries: HistoryEntry[] = [];
  const deltaEntries = new Map<string, HistoryEntry>();

  for (const row of [...rows].sort(compareHistoryRows)) {
    if (row.type === "error") continue;

    if (
      isDeltaEntryType(row.type) &&
      row.delta !== null &&
      row.requestId !== null &&
      row.eventIndex !== null
    ) {
      const key = `${row.requestId}\u0000${row.eventIndex}`;
      const existing = deltaEntries.get(key);
      if (existing) {
        existing.text = `${existing.text ?? ""}${row.delta}`;
        existing.sourceRowPositions.push(row.rowPosition);
        continue;
      }

      const entry: HistoryEntry = {
        id: `${conversationId}:entry:${row.requestId}:${row.eventIndex}`,
        type: row.type,
        role: row.role,
        text: row.delta,
        sessionId: row.sessionId,
        requestId: row.requestId,
        eventIndex: row.eventIndex,
        sourceRowPositions: [row.rowPosition],
      };
      deltaEntries.set(key, entry);
      entries.push(entry);
      continue;
    }

    if (row.type === "user") {
      entries.push({
        id: `${conversationId}:entry:row:${row.rowPosition}`,
        type: "user",
        role: "user",
        text: row.content ?? "",
        sessionId: row.sessionId,
        requestId: row.requestId,
        eventIndex: null,
        sourceRowPositions: [row.rowPosition],
      });
      continue;
    }

    if (row.type === "toolresult" || row.type === "toolresults") {
      entries.push({
        id: `${conversationId}:entry:row:${row.rowPosition}`,
        type: "toolresult",
        role: "tool",
        text: row.content === null ? "" : toolResultText(row.content),
        sessionId: row.sessionId,
        requestId: row.requestId,
        eventIndex: null,
        sourceRowPositions: [row.rowPosition],
      });
      continue;
    }

    // Legacy/fallback row: still show it as one logical entry instead of losing it.
    entries.push({
      id: `${conversationId}:entry:row:${row.rowPosition}`,
      type: row.type,
      role: row.role,
      text: row.content ?? row.delta ?? "",
      sessionId: row.sessionId,
      requestId: row.requestId,
      eventIndex: row.eventIndex,
      sourceRowPositions: [row.rowPosition],
    });
  }

  return markLogicalSpacing(entries);
}
