// Lớp HTTP tương thích OpenAI, dùng chung cho các provider.
// Không thêm dep: dùng fetch toàn cục (Node >= 18).
import {
  LlmError,
  type ChatMessage,
  type ChatToolDefinition,
  type ProviderName,
} from "../types.js";

export interface ChatCompletionsRequest {
  provider: ProviderName;
  baseUrl: string;
  apiKey: string;
  model: string;
  messages: ChatMessage[];
  tools?: ChatToolDefinition[];
  temperature?: number;
  maxTokens?: number;
  timeoutMs?: number;
  signal?: AbortSignal;
}

export function pickUsage(raw: unknown): {
  promptTokens?: number;
  completionTokens?: number;
  reasoningTokens?: number;
  totalTokens?: number;
} {
  if (typeof raw !== "object" || raw === null) return {};
  const usage = (raw as Record<string, unknown>).usage;
  if (typeof usage !== "object" || usage === null) return {};
  const u = usage as Record<string, unknown>;
  const num = (v: unknown): number | undefined =>
    typeof v === "number" ? v : undefined;
  const completionDetails =
    typeof u.completion_tokens_details === "object" &&
    u.completion_tokens_details !== null
      ? (u.completion_tokens_details as Record<string, unknown>)
      : undefined;
  return {
    promptTokens: num(u.prompt_tokens),
    completionTokens: num(u.completion_tokens),
    reasoningTokens:
      num(completionDetails?.reasoning_tokens) ?? num(u.reasoning_tokens),
    totalTokens: num(u.total_tokens),
  };
}

export function pickReasoningDelta(raw: unknown): string {
  if (typeof raw !== "object" || raw === null) return "";
  const choices = (raw as Record<string, unknown>).choices;
  if (!Array.isArray(choices)) return "";
  const first = choices[0];
  if (typeof first !== "object" || first === null) return "";
  const delta = (first as Record<string, unknown>).delta;
  if (typeof delta !== "object" || delta === null) return "";
  const d = delta as Record<string, unknown>;

  for (const key of ["reasoning_content", "reasoning", "reasoning_text"]) {
    const value = d[key];
    if (typeof value === "string") return value;
  }
  return "";
}

export interface StreamChunk {
  /** JSON đã parse của 1 SSE `data:` (null ở sự kiện done). */
  json: unknown;
  done: boolean;
}

function parseSsePayload(
  provider: ProviderName,
  payload: string,
): { json: unknown; done: boolean } {
  if (payload === "[DONE]") return { json: null, done: true };
  try {
    return { json: JSON.parse(payload), done: false };
  } catch {
    throw new LlmError(`${provider} trả SSE không parse được: ${payload.slice(0, 200)}`, {
      provider,
      code: "BAD_SSE",
    });
  }
}

/**
 * Stream delta thô theo chuẩn SSE của endpoint /chat/completions
 * (`stream: true`). Yield từng chunk JSON gốc theo đúng thứ tự,
 * kết thúc bằng 1 chunk `{ json: null, done: true }`.
 */
export async function* streamCompletions(
  req: ChatCompletionsRequest,
): AsyncGenerator<StreamChunk, void, void> {
  const url = `${req.baseUrl.replace(/\/+$/, "")}/chat/completions`;
  const body: Record<string, unknown> = {
    model: req.model,
    messages: req.messages,
    stream: true,
    // Nhờ provider gửi usage ở chunk cuối (OpenAI/DeepSeek đều hỗ trợ).
    stream_options: { include_usage: true },
  };
  if (req.tools?.length) body.tools = req.tools;
  if (req.temperature !== undefined) body.temperature = req.temperature;
  if (req.maxTokens !== undefined) body.max_tokens = req.maxTokens;

  const inactivityMs = req.timeoutMs ?? 60_000;
  const controller = new AbortController();
  const onExternalAbort = (): void => {
    controller.abort(req.signal?.reason);
  };
  if (req.signal?.aborted) {
    onExternalAbort();
  } else {
    req.signal?.addEventListener("abort", onExternalAbort, { once: true });
  }
  let timeout: ReturnType<typeof setTimeout> | null = null;
  let timedOut = false;

  const armInactivityTimeout = (): void => {
    if (timeout !== null) clearTimeout(timeout);
    timeout = setTimeout(() => {
      timedOut = true;
      controller.abort();
    }, inactivityMs);
  };

  armInactivityTimeout();

  let res: Response;
  try {
    res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Accept: "text/event-stream",
        Authorization: `Bearer ${req.apiKey}`,
      },
      body: JSON.stringify(body),
      signal: controller.signal,
    });
    armInactivityTimeout();
  } catch (err) {
    if (timeout !== null) clearTimeout(timeout);
    req.signal?.removeEventListener("abort", onExternalAbort);
    if (timedOut) {
      throw new LlmError(
        `${req.provider} stream timeout: không nhận dữ liệu mới trong ${inactivityMs} ms`,
        { provider: req.provider, code: "STREAM_INACTIVITY_TIMEOUT" },
      );
    }
    throw new LlmError(
      `Network failure calling ${req.provider}: ${(err as Error).message}`,
      { provider: req.provider, code: "NETWORK_ERROR" },
    );
  }

  if (!res.ok || !res.body) {
    if (timeout !== null) clearTimeout(timeout);
    req.signal?.removeEventListener("abort", onExternalAbort);
    const detail = await res.text().then(
      (t) => t.slice(0, 500),
      () => `HTTP ${res.status}`,
    );
    throw new LlmError(`${req.provider} rejected request: ${detail}`, {
      provider: req.provider,
      status: res.status,
      code: "HTTP_ERROR",
    });
  }

  const reader = res.body.getReader();
  const decoder = new TextDecoder();
  let buf = "";

  const drainLines = function* (): Generator<{ json: unknown; done: boolean }> {
    let idx: number;
    while ((idx = buf.indexOf("\n")) >= 0) {
      const line = buf.slice(0, idx).trim();
      buf = buf.slice(idx + 1);
      if (!line || line.startsWith(":")) continue; // keep-alive/comment
      if (!line.startsWith("data:")) continue; // chỉ quan tâm data:
      yield parseSsePayload(req.provider, line.slice(5).trim());
    }
  };

  try {
    while (true) {
      const { done, value } = await reader.read();
      if (value && value.byteLength > 0) {
        // Bất kỳ traffic nào (content, reasoning, usage, keep-alive) đều chứng
        // minh stream còn sống, nên reset inactivity timeout.
        armInactivityTimeout();
      }

      buf += decoder.decode(value ?? new Uint8Array(), { stream: !done });
      for (const chunk of drainLines()) {
        yield chunk;
        if (chunk.done) return;
      }
      if (done) {
        // Server đóng stream mà không gửi [DONE]: flush phần còn lại
        // rồi vẫn chốt bằng sự kiện done để consumer thống nhất.
        const tail = buf.trim();
        buf = "";
        if (tail.startsWith("data:")) {
          const chunk = parseSsePayload(req.provider, tail.slice(5).trim());
          yield chunk;
          if (chunk.done) return;
        } else if (tail && !tail.startsWith(":")) {
          throw new LlmError(
            `${req.provider} đóng stream giữa chừng: ${tail.slice(0, 200)}`,
            { provider: req.provider, code: "TRUNCATED_STREAM" },
          );
        }
        yield { json: null, done: true };
        return;
      }
    }
  } catch (err) {
    if (timedOut) {
      throw new LlmError(
        `${req.provider} stream timeout: không nhận dữ liệu mới trong ${inactivityMs} ms`,
        { provider: req.provider, code: "STREAM_INACTIVITY_TIMEOUT" },
      );
    }
    throw err;
  } finally {
    if (timeout !== null) clearTimeout(timeout);
    req.signal?.removeEventListener("abort", onExternalAbort);
    reader.releaseLock();
  }
}
