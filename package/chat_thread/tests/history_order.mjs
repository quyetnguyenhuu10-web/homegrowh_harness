import assert from "node:assert/strict";
import { createServer } from "vite";

const server = await createServer({ configFile: false, server: { middlewareMode: true } });
try {
  const { mergeHistoryRows } = await server.ssrLoadModule("/src/component/history_conversation/renderer/history_rows.ts");
  const { projectHistoryEntries } = await server.ssrLoadModule("/src/component/view_history/renderer/project_history.ts");
  const row = (rowPosition, type, text, eventIndex = null) => ({
    rowPosition, type, role: type === "user" || type === "compaction" ? "user" : "assistant",
    content: eventIndex === null ? text : null, delta: eventIndex === null ? null : text,
    sessionId: "session", requestId: "request", eventIndex, createdAt: rowPosition,
  });
  const summary = row(1, "compaction", "summary");
  const summary2 = row(2, "compaction", "summary2");
  const user = row(3, "user", "prompt");
  const reply1 = row(4, "reply", "a", 1);
  const reply2 = row(5, "reply", "b", 1);
  const merged = mergeHistoryRows([user, reply1], [summary, reply2], [reply1, summary2]);
  assert.deepEqual(merged.map((entry) => entry.rowPosition), [1, 2, 3, 4, 5]);
  const entries = projectHistoryEntries("conversation", [...merged].reverse());
  assert.deepEqual(entries.map((entry) => entry.text), ["summary", "summary2", "prompt", "ab"]);
  assert.deepEqual(entries.at(-1).sourceRowPositions, [4, 5]);
  console.log("history_order: passed");
} finally {
  await server.close();
}
