import type { OpenAIToolResultMessage } from "../_shared/openai_executable_tool.js";
import type {
  TypeScriptToolHostRequest,
  TypeScriptToolHostResponse,
} from "../_shared/openai_typescript_tool.js";
import { mergeReadFiles, readFilesFor } from "../_shared/read_state.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

type ToolExecutor = (
  toolCall: TypeScriptToolHostRequest["toolCall"],
  context: ToolExecutionContext,
) => Promise<OpenAIToolResultMessage>;

const executorLoaders: Record<string, () => Promise<ToolExecutor>> = {
  read: async () => (await import("../read_file/logic.js")).executeReadFileToolCall,
  write: async () => (await import("../write_file/logic.js")).executeWriteFileToolCall,
  glob: async () => (await import("../glob/logic.js")).executeGlobToolCall,
  grep: async () => (await import("../grep/logic.js")).executeGrepToolCall,
  webfetch: async () => (await import("../webfetch/logic.js")).executeWebfetchToolCall,
};

async function readStdin(): Promise<string> {
  const chunks: Buffer[] = [];
  for await (const chunk of process.stdin) {
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }
  return Buffer.concat(chunks).toString("utf8");
}

async function executeRequest(
  request: TypeScriptToolHostRequest,
): Promise<TypeScriptToolHostResponse> {
  if (request.version !== 1) {
    return { ok: false, error: `Unsupported tool host version: ${String(request.version)}` };
  }
  const loadExecutor = executorLoaders[request.tool];
  if (!loadExecutor) {
    return { ok: false, error: `Unsupported TypeScript tool: ${request.tool}` };
  }

  const context: ToolExecutionContext = {
    repositoryPath: request.context.repositoryPath,
    ...(request.context.conversationId === undefined
      ? {}
      : { conversationId: request.context.conversationId }),
    ...(request.context.requestId === undefined
      ? {}
      : { requestId: request.context.requestId }),
  };

  mergeReadFiles(
    request.readFiles,
    context.conversationId,
    context.requestId,
  );

  try {
    const execute = await loadExecutor();
    const result = await execute(request.toolCall, context);
    return {
      ok: true,
      result,
      readFiles: readFilesFor(context.conversationId, context.requestId),
    };
  } catch (error) {
    return {
      ok: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}

async function main(): Promise<void> {
  let response: TypeScriptToolHostResponse;
  try {
    const raw = await readStdin();
    const request = JSON.parse(raw) as TypeScriptToolHostRequest;
    response = await executeRequest(request);
  } catch (error) {
    response = {
      ok: false,
      error: error instanceof Error ? error.message : String(error),
    };
  }
  process.stdout.write(JSON.stringify(response));
}

void main();
