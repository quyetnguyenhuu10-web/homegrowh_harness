// Phiên hội thoại: mở 1 lần, sống xuyên suốt nhiều lượt LLM,
// CHỈ kết thúc khi chủ động gọi close() — LLM trả xong response
// cũng không tự đóng (khác gọi request/response một lần rồi thôi).
import {
  SessionEntry,
  SessionEntryInput,
  SessionError,
} from "./types.js";

export interface SessionOptions {
  /** Id phiên. Không đưa thì tự sinh. */
  id?: string;
  /** Gọi mỗi khi có entry mới (kể cả trước khi đóng). */
  onEntry?: (entry: SessionEntry) => void;
  /** Gọi đúng 1 lần khi close(). */
  onClose?: (info: { id: string; reason?: string }) => void;
}

export interface ConversationSession {
  readonly id: string;
  readonly closed: boolean;
  /**
   * MỘT api duy nhất để định nghĩa cả 4 loại:
   * user message, assistant, tool_call, tool_result.
   * Ném SessionError nếu phiên đã đóng.
   */
  push(entry: SessionEntryInput): SessionEntry;
  /** Tiện ích: push tin nhắn user. */
  user(content: string, name?: string): SessionEntry;
  /** Tiện ích: push câu trả lời assistant. */
  assistant(content: string, model?: string): SessionEntry;
  /** Tiện ích: push yêu cầu gọi tool. */
  toolCall(callId: string, tool: string, args: unknown): SessionEntry;
  /** Tiện ích: push kết quả tool. */
  toolResult(
    callId: string,
    tool: string,
    result: unknown,
    error?: string,
  ): SessionEntry;
  /** Toàn bộ lịch sử theo thứ tự push. */
  history(): SessionEntry[];
  /**
   * Cách DUY NHẤT để kết thúc phiên socket.
   * Lũy tiến (gọi nhiều lần cũng chỉ đóng 1 lần).
   */
  close(reason?: string): void;
  /** Đăng ký nghe sự kiện đóng, trả về hàm hủy đăng ký. */
  onClose(cb: (info: { id: string; reason?: string }) => void): () => void;
  /** Hỗ trợ `using session = openSession()` — hết khối tự close(). */
  [Symbol.dispose](): void;
}

function newId(): string {
  return `sess_${Date.now().toString(36)}_${Math.random()
    .toString(36)
    .slice(2, 10)}`;
}

/** Mở 1 phiên hội thoại mới (đang mở cho tới khi close()). */
export function openSession(opts: SessionOptions = {}): ConversationSession {
  const id = opts.id ?? newId();
  const entries: SessionEntry[] = [];
  const closeListeners = new Set<(info: { id: string; reason?: string }) => void>();
  if (opts.onClose) closeListeners.add(opts.onClose);
  let isClosed = false;
  let closeReason: string | undefined;

  function mustOpen(): void {
    if (isClosed) {
      throw new SessionError(
        `Phiên "${id}" đã đóng${closeReason ? ` (lý do: ${closeReason})` : ""} — không push thêm được.`,
      );
    }
  }

  const session: ConversationSession = {
    id,
    get closed() {
      return isClosed;
    },

    push(entry: SessionEntryInput): SessionEntry {
      mustOpen();
      const full = { ...entry, at: Date.now() } as SessionEntry;
      entries.push(full);
      opts.onEntry?.(full);
      return full;
    },

    user(content: string, name?: string): SessionEntry {
      return session.push(name === undefined ? { kind: "user", content } : { kind: "user", content, name });
    },

    assistant(content: string, model?: string): SessionEntry {
      return session.push(
        model === undefined ? { kind: "assistant", content } : { kind: "assistant", content, model },
      );
    },

    toolCall(callId: string, tool: string, args: unknown): SessionEntry {
      return session.push({ kind: "tool_call", callId, tool, args });
    },

    toolResult(
      callId: string,
      tool: string,
      result: unknown,
      error?: string,
    ): SessionEntry {
      return session.push(
        error === undefined
          ? { kind: "tool_result", callId, tool, result }
          : { kind: "tool_result", callId, tool, result, error },
      );
    },

    history(): SessionEntry[] {
      return [...entries];
    },

    close(reason?: string): void {
      if (isClosed) return;
      isClosed = true;
      closeReason = reason;
      const info = { id, reason };
      for (const cb of [...closeListeners]) {
        try {
          cb(info);
        } catch {
          // Listener lỗi không được phá quá trình đóng phiên.
        }
      }
      closeListeners.clear();
    },

    onClose(cb): () => void {
      if (isClosed) {
        cb({ id, reason: closeReason });
        return () => {};
      }
      closeListeners.add(cb);
      return () => {
        closeListeners.delete(cb);
      };
    },

    [Symbol.dispose](): void {
      session.close("dispose");
    },
  };

  return session;
}
