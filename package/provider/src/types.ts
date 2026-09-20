// Kiểu dùng chung cho API LLM thống nhất.

export type ProviderName = "openai" | "deepseek" | "ollama" | "custom";

export type ChatRole = "system" | "user" | "assistant" | "tool";

export type JsonObject = Record<string, unknown>;
export type JsonArguments = JsonObject | JsonObject[];

export interface ChatToolCall {
  id: string;
  type: "function";
  function: {
    name: string;
    arguments: JsonArguments;
  };
}

export interface ChatToolDefinition {
  type: "function";
  function: {
    name: string;
    description?: string;
    parameters: Record<string, unknown>;
  };
}

export interface ChatMessage {
  role: ChatRole;
  content: string | null;
  /** OpenAI-compatible reasoning channel của assistant history. */
  reasoning_content?: string;
  name?: string;
  tool_calls?: ChatToolCall[];
  tool_call_id?: string;
}

/** Một khuôn gọi duy nhất — router chọn provider + key từ đây. */
export interface ChatOptions {
  /** Khóa router: chọn implementation của provider nào. */
  provider: ProviderName;
  messages: ChatMessage[];
  /** Function tools gửi nguyên theo OpenAI-compatible chat completions schema. */
  tools?: ChatToolDefinition[];
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
  /** Hủy request đang chạy từ caller. */
  signal?: AbortSignal;
}

export interface ChatUsage {
  promptTokens?: number;
  completionTokens?: number;
  /** Token nội bộ dành cho reasoning. */
  reasoningTokens?: number;
  totalTokens?: number;
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
  /** Mẩu reasoning provider có expose qua OpenAI-compatible delta. Không dùng làm answer text. */
  reasoningDelta?: string;
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
  /** Một model invocation duy nhất, stream delta thô theo thứ tự upstream. */
  call(
    input: Omit<ChatOptions, "provider"> & { apiKey: string; baseUrl?: string },
  ): AsyncIterable<StreamDelta>;
}
