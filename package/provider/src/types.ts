// Kiểu dùng chung cho API LLM thống nhất.

export type ProviderName = "openai" | "deepseek" | "ollama";

export type ChatRole = "system" | "user" | "assistant";

export interface ChatMessage {
  role: ChatRole;
  content: string;
  name?: string;
}

/** Một khuôn gọi duy nhất — router chọn provider + key từ đây. */
export interface ChatOptions {
  /** Khóa router: chọn implementation của provider nào. */
  provider: ProviderName;
  messages: ChatMessage[];
  /**
   * Field BẮT BUỘC chọn model cho lần gọi. Id lấy trong registry
   * models.ts (xem listModels()). API không có model mặc định.
   */
  model: string;
  /** Ghi đè từng lần gọi. Không có thì lấy <PROVIDER>_API_KEY trong .env. */
  apiKey?: string;
  /** Ghi đè từng lần gọi. Không có thì dùng endpoint mặc định của provider. */
  baseUrl?: string;
  temperature?: number;
  maxTokens?: number;
  timeoutMs?: number;
}

export interface ChatUsage {
  promptTokens?: number;
  completionTokens?: number;
  totalTokens?: number;
}

export interface ChatResult {
  provider: ProviderName;
  model: string;
  content: string;
  usage: ChatUsage;
  raw: unknown;
}

/**
 * Một mẩu delta THÔ stream từ provider (1 SSE chunk).
 * Vòng lặp `for await` nhận từng mẩu theo đúng thứ tự provider trả về,
 * kết thúc bằng 1 sự kiện `done: true` (tương ứng `[DONE]`).
 */
export interface StreamDelta {
  provider: ProviderName;
  model: string;
  /** Mẩu text trong chunk này (có thể rỗng ở chunk chỉ báo finish/usage). */
  delta: string;
  /** JSON gốc của SSE chunk (null ở sự kiện done). */
  raw: unknown;
  finishReason?: string | null;
  usage: ChatUsage;
  /** true ở sự kiện cuối ([DONE]). */
  done: boolean;
}

export class LlmError extends Error {
  provider: ProviderName | "unknown";
  status?: number;
  code?: string;

  constructor(
    message: string,
    opts: { provider?: ProviderName | "unknown"; status?: number; code?: string } = {},
  ) {
    super(message);
    this.name = "LlmError";
    this.provider = opts.provider ?? "unknown";
    this.status = opts.status;
    this.code = opts.code;
  }
}

/** Mọi provider trong openai/ và deepseek/ đều implement interface này. */
export interface LlmProvider {
  name: ProviderName;
  defaultBaseUrl: string;
  chat(
    input: Omit<ChatOptions, "provider"> & { apiKey: string; baseUrl?: string },
  ): Promise<ChatResult>;
  /** Stream delta thô (SSE), giữ nguyên thứ tự + JSON gốc từng chunk. */
  chatStream(
    input: Omit<ChatOptions, "provider"> & { apiKey: string; baseUrl?: string },
  ): AsyncIterable<StreamDelta>;
}
