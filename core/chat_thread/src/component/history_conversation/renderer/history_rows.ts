import type { HistoryRow } from "../history_contract";

export function compareHistoryRows(left: HistoryRow, right: HistoryRow): number {
  return left.rowPosition - right.rowPosition;
}

/** Merge row notifications and snapshots using the order supplied by Database. */
export function mergeHistoryRows(...collections: ReadonlyArray<readonly HistoryRow[]>): HistoryRow[] {
  const byPosition = new Map<number, HistoryRow>();
  for (const rows of collections) {
    for (const row of rows) byPosition.set(row.rowPosition, row);
  }
  return [...byPosition.values()].sort(compareHistoryRows);
}
