// Kiểu entry trong một phiên hội thoại.
// MỘT api push() duy nhất định nghĩa được cả 4 loại dưới đây.

/** Tin nhắn của người dùng. */
export interface UserEntry {
  kind: "user";
  content: string;
  name?: string;
  /** Epoch ms, tự điền khi push. */
  at: number;
}

/** Câu trả lời của assistant (LLM). */
export interface AssistantEntry {
  kind: "assistant";
  content: string;
  model?: string;
  at: number;
}

/** LLM yêu cầu gọi tool. */
export interface ToolCallEntry {
  kind: "tool_call";
  /** Id để ghép cặp với tool_result. */
  callId: string;
  tool: string;
  args: unknown;
  at: number;
}

/** Kết quả thực thi tool (đưa lại cho LLM). */
export interface ToolResultEntry {
  kind: "tool_result";
  callId: string;
  tool: string;
  result: unknown;
  error?: string;
  at: number;
}

export type SessionEntry =
  | UserEntry
  | AssistantEntry
  | ToolCallEntry
  | ToolResultEntry;

/** Đầu vào của push(): giống entry nhưng không cần `at` (tự đóng dấu giờ). */
export type SessionEntryInput =
  | Omit<UserEntry, "at">
  | Omit<AssistantEntry, "at">
  | Omit<ToolCallEntry, "at">
  | Omit<ToolResultEntry, "at">;

export type EntryKind = SessionEntry["kind"];

export class SessionError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "SessionError";
  }
}
