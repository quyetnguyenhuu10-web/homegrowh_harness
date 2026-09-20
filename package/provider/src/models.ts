import registryJson from "./models.json" with { type: "json" };

import type { ProviderName } from "./types.js";

export type BuiltInProviderName = Exclude<ProviderName, "custom">;

/** Thông tin 1 model trong registry JSON. */
export interface ModelInfo {
  /** Id gửi lên API (ví dụ "deepseek-chat"). */
  id: string;
  /** Tên hiển thị ngắn gọn. */
  label: string;
  /** Ghi chú khả năng (reasoning, giá rẻ, ...). */
  note?: string;
  /** Giới hạn context của đúng một model request, đơn vị token. */
  contextWindowTokens: number;
}

export interface ModelGroupInfo {
  provider: BuiltInProviderName;
  label: string;
  models: readonly ModelInfo[];
}

export interface ModelSelectionInfo {
  provider: BuiltInProviderName;
  model: string;
}

export interface ModelContextLimit {
  tokens: number;
  estimatedCharacters: number;
  charactersPerToken: number;
}

interface ModelRegistryFile {
  version: 1;
  defaultModel: ModelSelectionInfo;
  groups: ModelGroupInfo[];
}

export const APPROXIMATE_CHARACTERS_PER_TOKEN = 4;

const registry = registryJson as ModelRegistryFile;

function contextLimit(tokens: number): ModelContextLimit {
  if (!Number.isInteger(tokens) || tokens <= 0) {
    throw new Error(`Context window không hợp lệ: ${tokens}`);
  }
  return {
    tokens,
    estimatedCharacters: tokens * APPROXIMATE_CHARACTERS_PER_TOKEN,
    charactersPerToken: APPROXIMATE_CHARACTERS_PER_TOKEN,
  };
}

/** Snapshot các nhóm built-in lấy trực tiếp từ models.json. */
export function listModelGroups(): readonly ModelGroupInfo[] {
  return registry.groups;
}

/** Model mặc định cũng được khai báo trong JSON, không hard-code ở renderer. */
export function getDefaultModelSelection(): ModelSelectionInfo {
  return { ...registry.defaultModel };
}

/** Liệt kê id model của 1 provider built-in. Custom được quản lý ở app registry. */
export function listModels(provider: ProviderName): readonly string[] {
  if (provider === "custom") return [];
  return (
    registry.groups.find((group) => group.provider === provider)?.models.map((m) => m.id) ??
    []
  );
}

/** Model built-in có trong registry JSON không (dùng để cảnh báo, không chặn). */
export function isKnownModel(provider: ProviderName, model: string): boolean {
  return listModels(provider).includes(model);
}

/**
 * API truy vấn giới hạn context của một model request.
 * Custom model truyền contextWindowTokens đã resolve từ JSON riêng của app.
 */
export function getModelContextLimit(
  provider: ProviderName,
  modelId: string,
  contextWindowTokens?: number,
): ModelContextLimit {
  if (provider === "custom") {
    if (contextWindowTokens === undefined) {
      throw new Error(`Custom model chưa có context limit: ${modelId}`);
    }
    return contextLimit(contextWindowTokens);
  }

  const info = registry.groups
    .find((group) => group.provider === provider)
    ?.models.find((item) => item.id === modelId);
  if (!info) {
    throw new Error(`Model chưa được khai báo context limit: ${provider}/${modelId}`);
  }
  return contextLimit(info.contextWindowTokens);
}
