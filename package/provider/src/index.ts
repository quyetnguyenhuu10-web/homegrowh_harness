// Public API duy nhất của provider: gọi đúng một model invocation và trả stream.
import dotenv from "dotenv";
import { fileURLToPath } from "node:url";
import { createDeepSeekClient } from "./deepseek/index.js";
import { createOllamaClient } from "./local/ollama/index.js";
import { createOpenAIClient } from "./openai/index.js";
import {
  getDefaultModelSelection,
  getModelContextLimit,
  listModelGroups,
} from "./models.js";
import { LlmError } from "./types.js";
import type {
  ChatMessage,
  ChatOptions,
  ChatToolCall,
  ChatToolDefinition,
  JsonArguments,
  JsonObject,
  LlmProvider,
  ProviderName,
  StreamDelta,
} from "./types.js";

dotenv.config({ path: fileURLToPath(new URL("../.env", import.meta.url)) });

type EnvironmentProviderName = Exclude<ProviderName, "custom">;

const ENV_KEY: Record<EnvironmentProviderName, string> = {
  openai: "OPENAI_API_KEY",
  deepseek: "DEEPSEEK_API_KEY",
  ollama: "OLLAMA_API_KEY",
};

function keyFromEnv(provider: ProviderName): string {
  if (provider === "custom") {
    throw new LlmError(
      'Missing API key for "custom". Custom model must provide opts.apiKey.',
      { provider, code: "MISSING_API_KEY" },
    );
  }
  const envKey = ENV_KEY[provider];
  const value = process.env[envKey]?.trim();
  // Ollama local không kiểm tra key: thiếu thì dùng chuỗi rỗng.
  if (!value) {
    if (provider === "ollama") return "";
    throw new LlmError(
      `Missing API key for "${provider}". Pass opts.apiKey or set ${envKey} in .env.`,
      { provider, code: "MISSING_API_KEY" },
    );
  }
  return value;
}

/** Internal router: dựng client cho đúng provider của lần gọi hiện tại. */
function getProvider(
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
  if (provider === "ollama") {
    return createOllamaClient({ apiKey, baseUrl: override.baseUrl });
  }
  if (!override.baseUrl?.trim()) {
    throw new LlmError('Custom model requires opts.baseUrl.', {
      provider,
      code: "MISSING_BASE_URL",
    });
  }
  return createOpenAIClient({
    apiKey,
    baseUrl: override.baseUrl,
    provider: "custom",
  });
}

/**
 * API DUY NHẤT: một lần gọi model. Provider chỉ route provider/key và trả
 * delta thô theo đúng thứ tự upstream; không biết session/request/db/tool-loop.
 */
export async function* callProvider(
  opts: ChatOptions,
): AsyncGenerator<StreamDelta, void, void> {
  if (!opts || !opts.provider) {
    throw new LlmError("callProvider() requires opts.provider", { code: "BAD_OPTIONS" });
  }
  if (!Array.isArray(opts.messages) || opts.messages.length === 0) {
    throw new LlmError("callProvider() requires a non-empty opts.messages array", {
      provider: opts.provider,
      code: "BAD_OPTIONS",
    });
  }
  if (!opts.model?.trim()) {
    throw new LlmError("callProvider() requires opts.model", {
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
  yield* provider.call({ ...rest, apiKey });
}

export type {
  ChatMessage,
  ChatOptions,
  ChatToolCall,
  ChatToolDefinition,
  JsonArguments,
  JsonObject,
  ProviderName,
  StreamDelta,
};
export { LlmError };
export { getDefaultModelSelection, getModelContextLimit, listModelGroups };
export type {
  BuiltInProviderName,
  ModelContextLimit,
  ModelGroupInfo,
  ModelInfo,
  ModelSelectionInfo,
} from "./models.js";

export default callProvider;
