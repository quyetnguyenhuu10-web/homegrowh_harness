import type { OpenAIToolResultMessage } from "../_shared/openai_executable_tool.js";
import type {
  TypeScriptToolHostError,
  TypeScriptToolHostRequest,
  TypeScriptToolHostResponse,
} from "../_shared/openai_typescript_tool.js";
import { mergeReadFiles, readFilesFor } from "../_shared/read_state.js";
import type { ToolExecutionContext } from "../_shared/native_tool.js";

type ToolExecutor = (
  toolCall: TypeScriptToolHostRequest["toolCall"],
  context: ToolExecutionContext,
) => Promise<OpenAIToolResultMessage>;

function serializeError(error: unknown): TypeScriptToolHostError {
  if (!(error instanceof Error)) {
    return { message: String(error) };
  }

  const systemError = error as NodeJS.ErrnoException & {
    path?: string;
    dest?: string;
  };
  return {
    message: error.message,
    name: error.name,
    ...(typeof systemError.code === "string" ? { code: systemError.code } : {}),
    ...(typeof systemError.errno === "number" ? { errno: systemError.errno } : {}),
    ...(typeof systemError.syscall === "string" ? { syscall: systemError.syscall } : {}),
    ...(typeof systemError.path === "string" ? { path: systemError.path } : {}),
    ...(typeof systemError.dest === "string" ? { dest: systemError.dest } : {}),
  };
}

const executorLoaders: Record<string, () => Promise<ToolExecutor>> = {
  read: async () => (await import("../read_file/logic.js")).executeReadFileToolCall,
  write: async () => (await import("../write_file/logic.js")).executeWriteFileToolCall,
  glob: async () => (await import("../glob/logic.js")).executeGlobToolCall,
  grep: async () => (await import("../grep/logic.js")).executeGrepToolCall,
  webfetch: async () => (await import("../webfetch/logic.js")).executeWebfetchToolCall,
  todowrite: async () => (await import("../todowrite/logic.js")).executeTodowriteToolCall,
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
    return {
      ok: false,
      error: { message: `Unsupported tool host version: ${String(request.version)}` },
    };
  }
  const loadExecutor = executorLoaders[request.tool];
  if (!loadExecutor) {
    return {
      ok: false,
      error: { message: `Unsupported TypeScript tool: ${request.tool}` },
    };
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
      error: serializeError(error),
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
      error: serializeError(error),
    };
  }
  process.stdout.write(JSON.stringify(response));
}

void main();
