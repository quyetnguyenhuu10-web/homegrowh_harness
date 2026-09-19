// Cầu nối phiên -> LLM: gọi LLM xong phiên VẪN MỞ.
// Quy ước duy nhất của package: LLM hết response không đóng gì cả,
// socket phiên chỉ kết thúc khi chủ động gọi session.close().
import type { ConversationSession } from "./session.js";
import type { SessionEntry } from "./types.js";

/** Kết quả 1 lượt gọi LLM do caller tự cung cấp (tránh phụ thuộc provider). */
export interface TurnLlmResult {
  content: string;
  model?: string;
  toolCalls?: Array<{ callId: string; tool: string; args: unknown }>;
}

/**
 * Chạy 1 lượt hội thoại: push user -> gọi LLM với toàn bộ lịch sử
 * -> push assistant (+ các tool_call nếu có) -> trả kết quả.
 * KHÔNG đóng phiên — caller tự session.close() khi xong việc.
 */
export async function completeTurn(
  session: ConversationSession,
  userContent: string,
  callLlm: (history: SessionEntry[]) => Promise<TurnLlmResult>,
): Promise<TurnLlmResult & { text: string }> {
  session.push({ kind: "user", content: userContent });
  const out = await callLlm(session.history());
  session.push(
    out.model === undefined
      ? { kind: "assistant", content: out.content }
      : { kind: "assistant", content: out.content, model: out.model },
  );
  for (const tc of out.toolCalls ?? []) {
    session.push({
      kind: "tool_call",
      callId: tc.callId,
      tool: tc.tool,
      args: tc.args,
    });
  }
  return { ...out, text: out.content };
}

/**
 * Ánh xạ lịch sử phiên sang message LLM dạng gọn
 * ({ role: user|assistant, content }) cho provider không hiểu tool.
 */
export function toLlmMessages(
  history: SessionEntry[],
): Array<{ role: "user" | "assistant"; content: string }> {
  return history.map((e) => {
    switch (e.kind) {
      case "user":
        return { role: "user" as const, content: e.content };
      case "assistant":
        return { role: "assistant" as const, content: e.content };
      case "tool_call":
        return {
          role: "assistant" as const,
          content: `[tool_call ${e.tool} ${e.callId}] ${JSON.stringify(e.args)}`,
        };
      case "tool_result":
        return {
          role: "user" as const,
          content: `[tool_result ${e.tool} ${e.callId}]${
            e.error ? ` ERROR: ${e.error}` : ""
          } ${JSON.stringify(e.result)}`,
        };
    }
  });
}
