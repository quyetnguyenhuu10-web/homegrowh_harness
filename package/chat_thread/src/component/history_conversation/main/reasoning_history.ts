import type {
  HistoryRow,
  ReasoningHistoryPolicy,
} from "../history_contract";

export const DEFAULT_REASONING_HISTORY_POLICY: ReasoningHistoryPolicy = {
  mode: "recent",
  requestCount: 5,
};

export function normalizeReasoningHistoryPolicy(
  policy: ReasoningHistoryPolicy | undefined,
): ReasoningHistoryPolicy {
  if (!policy || policy.mode === "all") {
    return policy?.mode === "all"
      ? { mode: "all" }
      : { ...DEFAULT_REASONING_HISTORY_POLICY };
  }

  const requestCount = Math.max(1, Math.floor(policy.requestCount));
  return { mode: "recent", requestCount };
}

export function selectReasoningRequestIds(
  rows: readonly HistoryRow[],
  policy: ReasoningHistoryPolicy,
): Set<string> {
  const normalized = normalizeReasoningHistoryPolicy(policy);
  const ids = new Set<string>();

  if (normalized.mode === "all") {
    for (const row of rows) {
      if (row.type === "reasoning" && row.request_id !== null) {
        ids.add(row.request_id);
      }
    }
    return ids;
  }

  for (const row of [...rows].sort((left, right) => right.id - left.id)) {
    if (
      row.type !== "reasoning" ||
      row.request_id === null ||
      ids.has(row.request_id)
    ) {
      continue;
    }
    ids.add(row.request_id);
    if (ids.size >= normalized.requestCount) break;
  }
  return ids;
}
