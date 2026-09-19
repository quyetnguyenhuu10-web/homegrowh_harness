// Provider Ollama local (endpoint /v1 tương thích OpenAI).
// Không cần key: Ollama local bỏ qua auth (để trống vẫn chạy).
import { chatCompletions, pickUsage, streamCompletions } from "../../shared/http.js";
import type {
  ChatMessage,
  ChatResult,
  LlmProvider,
  ProviderName,
  StreamDelta,
} from "../../types.js";

export const OLLAMA_DEFAULT_BASE_URL = "http://localhost:11434/v1";

export interface OllamaClientOptions {
  /** Thường để trống — Ollama local không kiểm tra key. */
  apiKey?: string;
  baseUrl?: string;
}

export function createOllamaClient(opts: OllamaClientOptions = {}): LlmProvider {
  const name: ProviderName = "ollama";
  const defaultBaseUrl = opts.baseUrl ?? OLLAMA_DEFAULT_BASE_URL;
  const apiKey = opts.apiKey ?? "";

  return {
    name,
    defaultBaseUrl,
    async chat(input): Promise<ChatResult> {
      const model = input.model;
      const messages: ChatMessage[] = input.messages;
      const out = await chatCompletions({
        provider: name,
        baseUrl: input.baseUrl ?? defaultBaseUrl,
        apiKey: input.apiKey || apiKey,
        model,
        messages,
        temperature: input.temperature,
        maxTokens: input.maxTokens,
        timeoutMs: input.timeoutMs,
      });
      return { provider: name, model, ...out };
    },
    async *chatStream(input): AsyncIterable<StreamDelta> {
      const model = input.model;
      for await (const chunk of streamCompletions({
        provider: name,
        baseUrl: input.baseUrl ?? defaultBaseUrl,
        apiKey: input.apiKey || apiKey,
        model,
        messages: input.messages as ChatMessage[],
        temperature: input.temperature,
        maxTokens: input.maxTokens,
        timeoutMs: input.timeoutMs,
      })) {
        if (chunk.done) {
          yield { provider: name, model, delta: "", raw: null, finishReason: null, usage: {}, done: true };
          continue;
        }
        const choice = (chunk.json as Record<string, unknown> | null)?.choices as
          | Array<Record<string, unknown>>
          | undefined;
        const first = choice?.[0];
        const deltaText =
          typeof (first?.delta as Record<string, unknown> | undefined)?.content === "string"
            ? ((first?.delta as Record<string, unknown>).content as string)
            : "";
        const finish =
          typeof first?.finish_reason === "string" || first?.finish_reason === null
            ? (first?.finish_reason as string | null)
            : null;
        yield {
          provider: name,
          model,
          delta: deltaText,
          raw: chunk.json,
          finishReason: finish,
          usage: pickUsage(chunk.json),
          done: false,
        };
      }
    },
  };
}
