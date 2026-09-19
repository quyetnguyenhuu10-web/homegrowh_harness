// Nơi duy nhất khai báo model cho mọi provider.
// API chọn model qua field `model` trong ChatOptions (kèm per-call override);
// bỏ trống thì lấy model mặc định bên dưới.
import type { ProviderName } from "./types.js";

/** Thông tin 1 model trong registry. */
export interface ModelInfo {
  /** Id gửi lên API (ví dụ "deepseek-chat"). */
  id: string;
  /** Tên hiển thị ngắn gọn. */
  label: string;
  /** Ghi chú khả năng (reasoning, giá rẻ, ...). */
  note?: string;
}

export const OPENAI_MODELS: readonly ModelInfo[] = [
  { id: "gpt-4o-mini", label: "GPT-4o mini", note: "nhanh, rẻ, mặc định" },
  { id: "gpt-4o", label: "GPT-4o", note: "mạnh, đa modal" },
  { id: "gpt-4.1-mini", label: "GPT-4.1 mini", note: "rẻ, context dài" },
  { id: "gpt-4.1", label: "GPT-4.1", note: "code tốt, context dài" },
  { id: "gpt-5-mini", label: "GPT-5 mini", note: "thế hệ mới, cân bằng" },
  { id: "gpt-5", label: "GPT-5", note: "mạnh nhất" },
];

export const DEEPSEEK_MODELS: readonly ModelInfo[] = [
  { id: "deepseek-chat", label: "DeepSeek V3", note: "chat tổng quát, mặc định" },
  { id: "deepseek-reasoner", label: "DeepSeek R1", note: "suy luận sâu (reasoning)" },
];

export const OLLAMA_MODELS: readonly ModelInfo[] = [
  { id: "qwen3:4b", label: "Qwen3 4B", note: "chạy local qua Ollama" },
];

const MODELS_BY_PROVIDER: Record<ProviderName, readonly ModelInfo[]> = {
  openai: OPENAI_MODELS,
  deepseek: DEEPSEEK_MODELS,
  ollama: OLLAMA_MODELS,
};

/** Liệt kê id model của 1 provider. */
export function listModels(provider: ProviderName): readonly string[] {
  return MODELS_BY_PROVIDER[provider].map((m) => m.id);
}

/** Model có trong registry không (dùng để cảnh báo, không chặn). */
export function isKnownModel(provider: ProviderName, model: string): boolean {
  return listModels(provider).includes(model);
}
