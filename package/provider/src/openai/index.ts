// Provider OpenAI (endpoint /chat/completions tương thích OpenAI).
import {
  pickReasoningDelta,
  pickUsage,
  streamCompletions,
} from "../shared/http.js";
import type {
  ChatMessage,
  LlmProvider,
  ProviderName,
  StreamDelta,
} from "../types.js";

export const OPENAI_DEFAULT_BASE_URL = "https://api.openai.com/v1";

export interface OpenAIClientOptions {
  apiKey: string;
  baseUrl?: string;
  /** Cho endpoint OpenAI-compatible custom dùng chung transport này. */
  provider?: ProviderName;
}

export function createOpenAIClient(opts: OpenAIClientOptions): LlmProvider {
  const name: ProviderName = opts.provider ?? "openai";
  const defaultBaseUrl = opts.baseUrl ?? OPENAI_DEFAULT_BASE_URL;

  return {
    name,
    defaultBaseUrl,
    async *call(input): AsyncIterable<StreamDelta> {
      const model = input.model;
      for await (const chunk of streamCompletions({
        provider: name,
        baseUrl: input.baseUrl ?? defaultBaseUrl,
        apiKey: input.apiKey,
        model,
        messages: input.messages as ChatMessage[],
        tools: input.tools,
        temperature: input.temperature,
        maxTokens: input.maxTokens,
        timeoutMs: input.timeoutMs,
        signal: input.signal,
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
          reasoningDelta: pickReasoningDelta(chunk.json),
          raw: chunk.json,
          finishReason: finish,
          usage: pickUsage(chunk.json),
          done: false,
        };
      }
    },
  };
}
