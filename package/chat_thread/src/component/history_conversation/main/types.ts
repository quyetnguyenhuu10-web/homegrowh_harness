// Kiểu dữ liệu chung cho 1 row lịch sử.
// Cố tình KHÔNG bó buộc vào message hay tool call: cột `type` là chuỗi
// tự do nên sau này push thêm loại mới (compaction, toolcall, message,
// ...) mà không phải đổi schema hay API.
import type { DatabaseSync } from "node:sqlite";

import type { HistoryRow } from "../history_contract";

/** Đầu vào của push(): không cần id/created_at (tự sinh). */
export interface PushRowInput {
  type: string;
  role?: string | null;
  content?: string | null;
  delta?: string | null;
  API_sessions: string;
  request_id?: string | null;
  event_index?: number | null;
}

/** Tay cầm 1 file .db (1 id). Mở bằng openHistory(). */
export interface HistoryHandle {
  /** Id phiên (= tên file .db không đuôi). */
  readonly id: string;
  /** Đường dẫn tuyệt đối tới file .db. */
  readonly file: string;
  /** Thêm 1 row, trả về row đầy đủ (kèm id). */
  push(row: PushRowInput): HistoryRow;
  /** Đọc row mới nhất trước (mặc định 100 dòng). */
  list(limit?: number, offset?: number): HistoryRow[];
  /** Đọc 1 row theo id, null khi không có. */
  get(rowId: number): HistoryRow | null;
  /** Xóa 1 row, true nếu row tồn tại. */
  deleteRow(rowId: number): boolean;
  /** Tổng số row hiện có. */
  count(): number;
  /** Xóa hết row nhưng giữ file .db. */
  clear(): void;
  /** Đóng kết nối db (nên gọi khi xong). */
  close(): void;
  /** Kết nối db thô, dùng khi cần câu lệnh đặc biệt. */
  readonly db: DatabaseSync;
}
