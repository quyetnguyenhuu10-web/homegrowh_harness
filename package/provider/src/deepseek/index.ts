// Provider DeepSeek (endpoint /chat/completions tương thích OpenAI).
import { chatCompletions, pickUsage, streamCompletions } from "../shared/http.js";
import type {
  ChatMessage,
  ChatResult,
  LlmProvider,
  ProviderName,
  StreamDelta,
} from "../types.js";

export const DEEPSEEK_DEFAULT_BASE_URL = "https://api.deepseek.com/v1";

export interface DeepSeekClientOptions {
  apiKey: string;
  baseUrl?: string;
}

export function createDeepSeekClient(opts: DeepSeekClientOptions): LlmProvider {
  const name: ProviderName = "deepseek";
  const defaultBaseUrl = opts.baseUrl ?? DEEPSEEK_DEFAULT_BASE_URL;

  return {
    name,
    defaultBaseUrl,
    async chat(input): Promise<ChatResult> {
      const model = input.model;
      const messages: ChatMessage[] = input.messages;
      const out = await chatCompletions({
        provider: name,
        baseUrl: input.baseUrl ?? defaultBaseUrl,
        apiKey: input.apiKey,
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
        apiKey: input.apiKey,
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
