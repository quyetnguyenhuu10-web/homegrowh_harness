// Lớp HTTP tương thích OpenAI, dùng chung cho các provider.
// Không thêm dep: dùng fetch toàn cục (Node >= 18) + AbortSignal.timeout.
import { LlmError, type ProviderName } from "../types.js";

export interface ChatCompletionsRequest {
  provider: ProviderName;
  baseUrl: string;
  apiKey: string;
  model: string;
  messages: Array<{ role: string; content: string; name?: string }>;
  temperature?: number;
  maxTokens?: number;
  timeoutMs?: number;
}

export function pickUsage(raw: unknown): {
  promptTokens?: number;
  completionTokens?: number;
  totalTokens?: number;
} {
  if (typeof raw !== "object" || raw === null) return {};
  const usage = (raw as Record<string, unknown>).usage;
  if (typeof usage !== "object" || usage === null) return {};
  const u = usage as Record<string, unknown>;
  const num = (v: unknown): number | undefined =>
    typeof v === "number" ? v : undefined;
  return {
    promptTokens: num(u.prompt_tokens),
    completionTokens: num(u.completion_tokens),
    totalTokens: num(u.total_tokens),
  };
}

export async function chatCompletions(
  req: ChatCompletionsRequest,
): Promise<{ content: string; usage: ReturnType<typeof pickUsage>; raw: unknown }> {
  const url = `${req.baseUrl.replace(/\/+$/, "")}/chat/completions`;
  const body: Record<string, unknown> = {
    model: req.model,
    messages: req.messages,
  };
  if (req.temperature !== undefined) body.temperature = req.temperature;
  if (req.maxTokens !== undefined) body.max_tokens = req.maxTokens;

  let res: Response;
  try {
    res = await fetch(url, {
      method: "POST",
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${req.apiKey}`,
      },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(req.timeoutMs ?? 60_000),
    });
  } catch (err) {
    throw new LlmError(
      `Network failure calling ${req.provider}: ${(err as Error).message}`,
      { provider: req.provider, code: "NETWORK_ERROR" },
    );
  }

  let data: unknown = null;
  try {
    data = await res.json();
  } catch {
    // Body không phải JSON: nhánh !res.ok bên dưới sẽ xử lý.
  }

  if (!res.ok) {
    const detail =
      typeof data === "object" && data !== null
        ? JSON.stringify(data).slice(0, 500)
        : `HTTP ${res.status}`;
    throw new LlmError(`${req.provider} rejected request: ${detail}`, {
      provider: req.provider,
      status: res.status,
      code: "HTTP_ERROR",
    });
  }

  const choice =
    typeof data === "object" && data !== null
      ? ((data as Record<string, unknown>).choices as Array<Record<string, unknown>> | undefined)?.[0]
      : undefined;
  const message = choice?.message as Record<string, unknown> | undefined;
  const content = message?.content;
  if (typeof content !== "string") {
    throw new LlmError(`${req.provider} returned no text content`, {
      provider: req.provider,
      status: res.status,
      code: "EMPTY_RESPONSE",
    });
  }

  return { content, usage: pickUsage(data), raw: data };
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
  if (req.temperature !== undefined) body.temperature = req.temperature;
  if (req.maxTokens !== undefined) body.max_tokens = req.maxTokens;

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
      signal: AbortSignal.timeout(req.timeoutMs ?? 60_000),
    });
  } catch (err) {
    throw new LlmError(
      `Network failure calling ${req.provider}: ${(err as Error).message}`,
      { provider: req.provider, code: "NETWORK_ERROR" },
    );
  }

  if (!res.ok || !res.body) {
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

  while (true) {
    const { done, value } = await reader.read();
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
}
