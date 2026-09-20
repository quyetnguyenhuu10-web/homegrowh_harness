import {
  editFileToolDefinition,
  executeEditFileToolCall,
} from "../edit_file/index";
import {
  executeReadFileToolCall,
  readFileToolDefinition,
} from "../read_file/index";
import {
  executeWriteFileToolCall,
  writeFileToolDefinition,
} from "../write_file/index";
import type {
  OpenAIFunctionToolCall,
  OpenAIFunctionToolDefinition,
  OpenAIToolResultMessage,
} from "../_shared/openai_executable_tool";

export interface ToolPlugin {
  definition: OpenAIFunctionToolDefinition;
  execute(toolCall: OpenAIFunctionToolCall): Promise<OpenAIToolResultMessage>;
}

function unsupportedToolResult(
  toolCall: OpenAIFunctionToolCall,
): OpenAIToolResultMessage {
  return {
    role: "tool",
    tool_call_id: toolCall.id,
    content: JSON.stringify({
      ok: false,
      error: {
        code: "unsupported_tool",
        message: `Tool không được đăng ký: ${toolCall.function.name}`,
      },
    }),
  };
}

/**
 * Registry duy nhất cho model-facing tool schema + executor.
 *
 * Context chỉ lấy definitions() để gửi model; runtime chỉ gọi execute().
 * Executor và metadata host-side không bị project vào model context.
 */
export class ToolRegistry {
  private readonly tools = new Map<string, ToolPlugin>();

  register(plugin: ToolPlugin): () => void {
    if (plugin.definition.type !== "function") {
      throw new Error("Tool registry hiện chỉ hỗ trợ function tool.");
    }

    const name = plugin.definition.function.name.trim();
    if (!name) {
      throw new Error("Tool phải có function.name không rỗng.");
    }
    if (this.tools.has(name)) {
      throw new Error(`Tool đã được đăng ký: ${name}`);
    }

    const registered: ToolPlugin = {
      definition: structuredClone(plugin.definition),
      execute: plugin.execute,
    };
    this.tools.set(name, registered);

    let disposed = false;
    return () => {
      if (disposed) return;
      disposed = true;
      if (this.tools.get(name) === registered) {
        this.tools.delete(name);
      }
    };
  }

  definitions(): OpenAIFunctionToolDefinition[] {
    return [...this.tools.values()].map(({ definition }) =>
      structuredClone(definition),
    );
  }

  execute(toolCall: OpenAIFunctionToolCall): Promise<OpenAIToolResultMessage> {
    const plugin = this.tools.get(toolCall.function.name);
    if (!plugin) {
      return Promise.resolve(unsupportedToolResult(toolCall));
    }
    return plugin.execute(toolCall);
  }
}

export function createDefaultToolRegistry(): ToolRegistry {
  const registry = new ToolRegistry();

  registry.register({
    definition: readFileToolDefinition,
    execute: executeReadFileToolCall,
  });
  registry.register({
    definition: writeFileToolDefinition,
    execute: executeWriteFileToolCall,
  });
  registry.register({
    definition: editFileToolDefinition,
    execute: executeEditFileToolCall,
  });

  return registry;
}

/** Registry dùng chung bởi context assembly và provider runtime. */
export const toolRegistry = createDefaultToolRegistry();
