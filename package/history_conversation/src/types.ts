// Kiểu dữ liệu chung cho 1 row lịch sử.
// Cố tình KHÔNG bó buộc vào message hay tool call: cột `type` là chuỗi
// tự do nên sau này push thêm loại mới (compaction, toolcall, message,
// ...) mà không phải đổi schema hay API.
import type { DatabaseSync } from "node:sqlite";

/** Một row đã lưu (có id + thời gian). */
export interface HistoryRow {
  /** Khóa tăng tự động của SQLite. */
  id: number;
  /** Loại entry tự do: "message" | "toolcall" | "compaction" | ... */
  type: string;
  /** Vai trò khi type là message: "user" | "assistant", còn lại để null. */
  role: string | null;
  /** Lỗi kèm theo (nếu có), null khi bình thường. */
  error_message: string | null;
  /** Nội dung chính (text / JSON stringify tùy caller). */
  content: string;
  /** Nguồn ảnh: đường dẫn file hoặc chuỗi rỗng, null khi không có. */
  source_image: string | null;
  /** Nguồn file/folder liên quan, null khi không có. */
  source_path: string | null;
  /** Epoch ms lúc push. */
  created_at: number;
}

/** Đầu vào của push(): không cần id/created_at (tự sinh). */
export interface PushRowInput {
  type: string;
  role?: string | null;
  error_message?: string | null;
  content: string;
  source_image?: string | null;
  source_path?: string | null;
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
