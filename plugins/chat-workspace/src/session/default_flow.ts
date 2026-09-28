export type SessionRuntimeState =
  | "request"
  | "response"
  | "tool"
  | "finished"
  | "closed";

export type JsonPrimitive = null | boolean | number | string;
export type JsonValue =
  | JsonPrimitive
  | JsonValue[]
  | { [key: string]: JsonValue };

export type SessionProvider = "openai" | "deepseek" | "bonsai";
export type SandboxNetwork = "none" | "internet_client";

export interface SessionRegisterConfig {
  api_key_raw: string;
  history: JsonValue[];
  session_current: JsonValue;
  tool_definitions: JsonValue[];
  provider: SessionProvider;
  endpoint: string;
  model_id: string;
  context_limit: number;
  compact_threshold: number;
  tool_result_timeout_ms: number;
  session_timeout_ms: number;
  compaction_prompt: string;
  workspace_path: string;
  tool_runtime_executable: string;
  sandbox_config: {
    read_only: string[];
    read_write: string[];
    network: SandboxNetwork;
  };
  refresh_workspace: boolean;
}

export interface SessionCommandCompletion {
  state: string;
  result: unknown;
}

/**
 * Browser-safe surface supplied by the desktop host.
 *
 * The host implementation owns @hh/session-client + IPC and resolves each
 * method only when the matching session_runtime command_finished arrives.
 */
export interface SessionClientBridge {
  registerSession(config: SessionRegisterConfig): Promise<SessionCommandCompletion>;
  declareRequest(): Promise<SessionCommandCompletion>;
  runRequest(): Promise<SessionCommandCompletion>;
  declareResponse(): Promise<SessionCommandCompletion>;
  runResponse(): Promise<SessionCommandCompletion>;
  declareTool(): Promise<SessionCommandCompletion>;
  runTool(): Promise<SessionCommandCompletion>;
  closeSession(): Promise<SessionCommandCompletion>;
}

export interface DefaultSessionResult {
  close: SessionCommandCompletion;
}

function requireState(value: string): SessionRuntimeState {
  switch (value) {
    case "request":
    case "response":
    case "tool":
    case "finished":
    case "closed":
      return value;
    default:
      throw new Error(`session runtime returned unknown state: ${value}`);
  }
}

/**
 * Default remote driver for the same state machine used by sessions::loop().
 *
 * C++ owns the Session and all transition/invariant checks. This function only
 * asks the runtime to declare + run the stage that matches the state returned
 * by C++.
 */
export async function runDefaultSession(
  client: SessionClientBridge,
  config: SessionRegisterConfig,
): Promise<DefaultSessionResult> {
  const registered = await client.registerSession(config);
  let state = requireState(registered.state);

  for (;;) {
    switch (state) {
      case "request": {
        await client.declareRequest();
        const completed = await client.runRequest();
        state = requireState(completed.state);
        break;
      }

      case "response": {
        await client.declareResponse();
        const completed = await client.runResponse();
        state = requireState(completed.state);
        break;
      }

      case "tool": {
        await client.declareTool();
        const completed = await client.runTool();
        state = requireState(completed.state);
        break;
      }

      case "finished": {
        const close = await client.closeSession();
        return { close };
      }

      case "closed":
        throw new Error("session runtime closed before reaching finished state");
    }
  }
}
