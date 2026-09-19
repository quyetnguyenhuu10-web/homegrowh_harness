// Điểm vào thống nhất cho LLM: MỘT api duy nhất, router theo provider + api key.
//
//   import { chat } from "./register.js";
//   const res = await chat({
//     provider: "deepseek",            // khóa router
//     messages: [{ role: "user", content: "hi" }],
//     // bỏ trống apiKey -> lấy DEEPSEEK_API_KEY trong .env
//   });
//
// Thứ tự lấy key mỗi lần gọi: opts.apiKey truyền trực tiếp > <PROVIDER>_API_KEY trong .env.
//   - OPENAI_API_KEY, DEEPSEEK_API_KEY
import dotenv from "dotenv";
import { fileURLToPath } from "node:url";
import { createDeepSeekClient } from "./deepseek/index.js";
import { createOllamaClient } from "./local/ollama/index.js";
import { listModels } from "./models.js";
import { createOpenAIClient } from "./openai/index.js";
import { LlmError } from "./types.js";
import type {
  ChatMessage,
  ChatOptions,
  ChatResult,
  LlmProvider,
  ProviderName,
  StreamDelta,
} from "./types.js";

dotenv.config({ path: fileURLToPath(new URL("../.env", import.meta.url)) });

const ENV_KEY: Record<ProviderName, string> = {
  openai: "OPENAI_API_KEY",
  deepseek: "DEEPSEEK_API_KEY",
  ollama: "OLLAMA_API_KEY",
};

function keyFromEnv(provider: ProviderName): string {
  const value = process.env[ENV_KEY[provider]]?.trim();
  // Ollama local không kiểm tra key: thiếu thì dùng chuỗi rỗng.
  if (!value) {
    if (provider === "ollama") return "";
    throw new LlmError(
      `Missing API key for "${provider}". Pass opts.apiKey or set ${ENV_KEY[provider]} in .env.`,
      { provider, code: "MISSING_API_KEY" },
    );
  }
  return value;
}

/** Dựng client cho provider. apiKey/baseUrl ghi đè .env + mặc định. */
export function getProvider(
  provider: ProviderName,
  override: { apiKey?: string; baseUrl?: string } = {},
): LlmProvider {
  const apiKey = override.apiKey?.trim() || keyFromEnv(provider);
  if (provider === "openai") {
    return createOpenAIClient({ apiKey, baseUrl: override.baseUrl });
  }
  if (provider === "deepseek") {
    return createDeepSeekClient({ apiKey, baseUrl: override.baseUrl });
  }
  return createOllamaClient({ apiKey, baseUrl: override.baseUrl });
}

export function listProviders(): ProviderName[] {
  return ["openai", "deepseek", "ollama"];
}

/**
 * Api thống nhất DUY NHẤT: một khuôn gọi, router chọn implementation theo
 * opts.provider và tự lấy key (truyền trực tiếp > .env).
 */
export async function chat(opts: ChatOptions): Promise<ChatResult> {
  if (!opts || !opts.provider) {
    throw new LlmError("chat() requires opts.provider", { code: "BAD_OPTIONS" });
  }
  if (!Array.isArray(opts.messages) || opts.messages.length === 0) {
    throw new LlmError("chat() requires a non-empty opts.messages array", {
      provider: opts.provider,
      code: "BAD_OPTIONS",
    });
  }
  if (!opts.model?.trim()) {
    throw new LlmError("chat() requires opts.model (API không có model mặc định)", {
      provider: opts.provider,
      code: "BAD_OPTIONS",
    });
  }
  const apiKey = opts.apiKey?.trim() || keyFromEnv(opts.provider);
  const provider = getProvider(opts.provider, {
    apiKey,
    baseUrl: opts.baseUrl,
  });
  const { provider: _drop, apiKey: _key, ...rest } = opts;
  void _drop;
  void _key;
  return provider.chat({ ...rest, apiKey });
}

/**
 * Stream delta THÔ: cùng 1 khuôn gọi như chat() nhưng trả từng mẩu SSE
 * theo đúng thứ tự provider (kèm JSON gốc), kết thúc bằng `done: true`.
 *
 *   for await (const ev of chatStream({ provider: "deepseek", messages })) {
 *     if (ev.done) break;
 *     process.stdout.write(ev.delta);
 *   }
 */
export async function* chatStream(
  opts: ChatOptions,
): AsyncGenerator<StreamDelta, void, void> {
  if (!opts || !opts.provider) {
    throw new LlmError("chatStream() requires opts.provider", { code: "BAD_OPTIONS" });
  }
  if (!Array.isArray(opts.messages) || opts.messages.length === 0) {
    throw new LlmError("chatStream() requires a non-empty opts.messages array", {
      provider: opts.provider,
      code: "BAD_OPTIONS",
    });
  }
  if (!opts.model?.trim()) {
    throw new LlmError("chatStream() requires opts.model (API không có model mặc định)", {
      provider: opts.provider,
      code: "BAD_OPTIONS",
    });
  }
  const apiKey = opts.apiKey?.trim() || keyFromEnv(opts.provider);
  const provider = getProvider(opts.provider, {
    apiKey,
    baseUrl: opts.baseUrl,
  });
  const { provider: _drop, apiKey: _key, ...rest } = opts;
  void _drop;
  void _key;
  yield* provider.chatStream({ ...rest, apiKey });
}

export type { ChatMessage, ChatOptions, ChatResult, LlmProvider, ProviderName, StreamDelta };
export type { ModelInfo } from "./models.js";
export { listModels, isKnownModel } from "./models.js";
export { LlmError };

export default { chat, chatStream, getProvider, listProviders, listModels };
