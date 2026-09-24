import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import {
  processFailure,
  type ProcessRunResult,
} from "../src/_runtime/process_runner.js";
import {
  errorPayload,
  toolResult,
} from "../src/_shared/openai_executable_tool.js";

const timeoutResult: ProcessRunResult = {
  code: 1460,
  stdout: "",
  stderr: "",
  started: true,
  timedOut: true,
  terminated: true,
  osErrorBeforeTermination: {
    code: 0,
    message: "The operation completed successfully.",
  },
  finalError: null,
  registryFinalError: null,
  pathErrors: [],
};

const failure = processFailure(timeoutResult);
assert.ok(failure);
assert.equal(failure.code, "process_timeout");
assert.deepEqual(failure.osError, timeoutResult.osErrorBeforeTermination);

const message = toolResult(
  "call-timeout",
  errorPayload("read", "call-timeout", failure.code, failure.message, {
    os_error: failure.osError,
    path_errors: failure.pathErrors,
  }),
);
const content = JSON.parse(message.content) as {
  ok: boolean;
  error: {
    code: string;
    os_error: { code: number; message: string };
  };
};

assert.equal(content.ok, false);
assert.equal(content.error.code, "process_timeout");
assert.deepEqual(content.error.os_error, timeoutResult.osErrorBeforeTermination);

const packageRoot = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const sourceRoot = join(packageRoot, "src");
const childProcessImports: string[] = [];

async function inspectDirectory(directory: string): Promise<void> {
  const entries = await readdir(directory, { withFileTypes: true });
  for (const entry of entries) {
    const path = join(directory, entry.name);
    if (entry.isDirectory()) {
      await inspectDirectory(path);
      continue;
    }
    if (!entry.isFile() || !entry.name.endsWith(".ts")) continue;
    const source = await readFile(path, "utf8");
    if (source.includes('"node:child_process"') || source.includes("'node:child_process'")) {
      childProcessImports.push(path);
    }
  }
}

await inspectDirectory(sourceRoot);
assert.deepEqual(
  childProcessImports.map((path) => resolve(path)),
  [resolve(sourceRoot, "_runtime", "process_runner.ts")],
  "Only process_runner.ts may import node:child_process",
);
