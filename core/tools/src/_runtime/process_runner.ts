import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import {
  decodeSandboxResult,
  encodeSandboxRequest,
  type SandboxFilesystemAccess,
  type SandboxFilesystemRule,
  type SandboxNetworkAccess,
  type SandboxOsError,
  type SandboxPathError,
  type SandboxPolicy,
} from "./sandbox_protocol.js";

export const DEFAULT_TOOL_PROCESS_TIMEOUT_MS = 120_000;

export interface ProcessRunRequest {
  executable: string;
  args: readonly string[];
  cwd: string;
  sandbox: SandboxPolicy;
  timeoutMs: number;
  refresh: boolean;
  stdin?: string;
  env?: NodeJS.ProcessEnv;
  signal?: AbortSignal;
}

export interface ProcessRunResult {
  code: number | null;
  stdout: string;
  stderr: string;
  started: boolean;
  timedOut: boolean;
  terminated: boolean;
  osErrorBeforeTermination: SandboxOsError | null;
  finalError: SandboxOsError | null;
  registryFinalError: SandboxOsError | null;
  pathErrors: SandboxPathError[];
}

export interface ProcessFailure {
  code: "process_timeout" | "sandbox_registry_failed" | "sandbox_process_failed";
  message: string;
  osError: SandboxOsError | null;
  pathErrors: SandboxPathError[];
}

export type {
  SandboxFilesystemAccess,
  SandboxFilesystemRule,
  SandboxNetworkAccess,
  SandboxPolicy,
};

function toolsRoot(): string {
  let current = dirname(fileURLToPath(import.meta.url));
  for (let depth = 0; depth < 8; depth += 1) {
    if (existsSync(resolve(current, "package.json"))) return current;
    const parent = dirname(current);
    if (parent === current) break;
    current = parent;
  }
  throw new Error("Không xác định được core/tools root cho sandbox process.");
}

function sandboxProcessPath(): string {
  const override = process.env.HOMEGROWPH_SANDBOX_PROCESS;
  if (override?.trim()) {
    const candidate = resolve(override.trim());
    if (!existsSync(candidate)) {
      throw new Error(`HOMEGROWPH_SANDBOX_PROCESS không tồn tại: ${candidate}`);
    }
    return candidate;
  }

  const root = resolve(toolsRoot(), "../..");
  const executable = process.platform === "win32" ? "sandbox_process.exe" : "sandbox_process";
  const candidate = resolve(root, "executable", executable);
  if (!existsSync(candidate)) {
    throw new Error(
      `Không tìm thấy sandbox process launcher: ${candidate}`,
    );
  }
  return candidate;
}

export function processFailure(result: ProcessRunResult): ProcessFailure | null {
  if (result.timedOut) {
    const os = result.osErrorBeforeTermination;
    return {
      code: "process_timeout",
      message: os
        ? `Tool process timed out; OS error before termination: ${os.code} (${os.message})`
        : "Tool process timed out; OS reported no error before termination.",
      osError: os,
      pathErrors: result.pathErrors,
    };
  }

  if (result.registryFinalError || (!result.started && result.pathErrors.length > 0)) {
    const os = result.registryFinalError;
    return {
      code: "sandbox_registry_failed",
      message: os
        ? `Sandbox registry failed: ${os.code} (${os.message})`
        : "Sandbox registry rejected one or more filesystem paths.",
      osError: os,
      pathErrors: result.pathErrors,
    };
  }

  if (result.finalError) {
    return {
      code: "sandbox_process_failed",
      message: `Sandbox process failed: ${result.finalError.code} (${result.finalError.message})`,
      osError: result.finalError,
      pathErrors: result.pathErrors,
    };
  }

  return null;
}

/**
 * Single process-creation authority for tools.
 *
 * Tool/protocol logic must not import node:child_process directly. This layer
 * only starts the trusted C++ sandbox broker; the broker creates the actual
 * target process under AppContainer/Landlock and owns timeout supervision.
 */
export function runProcess(request: ProcessRunRequest): Promise<ProcessRunResult> {
  if (request.signal?.aborted) {
    return Promise.reject(new Error("Tool process aborted"));
  }

  const launcher = sandboxProcessPath();
  const payload = encodeSandboxRequest({
    executable: request.executable,
    args: request.args,
    cwd: request.cwd,
    stdin: request.stdin,
    timeoutMs: request.timeoutMs,
    refresh: request.refresh,
    sandbox: request.sandbox,
  });

  return new Promise((resolvePromise, reject) => {
    const child = spawn(launcher, [], {
      cwd: request.cwd,
      env: request.env,
      windowsHide: true,
      stdio: ["pipe", "pipe", "pipe"],
    });

    const stdout: Buffer[] = [];
    let stderr = "";
    let settled = false;

    const cleanupAbort = (): void => {
      request.signal?.removeEventListener("abort", onAbort);
    };

    const finishReject = (error: Error): void => {
      if (settled) return;
      settled = true;
      cleanupAbort();
      reject(error);
    };

    const onAbort = (): void => {
      child.kill();
      finishReject(new Error("Tool process aborted"));
    };

    request.signal?.addEventListener("abort", onAbort, { once: true });

    child.stdout!.on("data", (chunk: Buffer) => {
      stdout.push(Buffer.from(chunk));
    });
    child.stderr!.setEncoding("utf8");
    child.stderr!.on("data", (chunk: string) => {
      stderr += chunk;
    });
    child.once("error", (error) => finishReject(error));
    child.once("close", (brokerCode) => {
      if (settled) return;
      settled = true;
      cleanupAbort();

      if (brokerCode !== 0) {
        reject(
          new Error(
            `Sandbox broker exited with code ${String(brokerCode)}${
              stderr.trim() ? `; stderr=${stderr.trim()}` : ""
            }`,
          ),
        );
        return;
      }

      try {
        const brokerResult = decodeSandboxResult(Buffer.concat(stdout));
        resolvePromise({
          code: brokerResult.exitCode < 0 ? null : brokerResult.exitCode,
          stdout: brokerResult.stdout,
          stderr: brokerResult.stderr,
          started: brokerResult.started,
          timedOut: brokerResult.timedOut,
          terminated: brokerResult.terminated,
          osErrorBeforeTermination: brokerResult.osErrorBeforeTermination,
          finalError: brokerResult.finalError,
          registryFinalError: brokerResult.registryFinalError,
          pathErrors: brokerResult.pathErrors,
        });
      } catch (error) {
        reject(
          new Error(
            `Invalid sandbox broker response: ${
              error instanceof Error ? error.message : String(error)
            }${stderr.trim() ? `; broker_stderr=${stderr.trim()}` : ""}`,
          ),
        );
      }
    });

    child.stdin!.end(payload);
  });
}
