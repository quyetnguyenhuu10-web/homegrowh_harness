import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import {
    type IpcConnection,
    listen,
} from "../lib/ipc-client/src/index.ts";

const opcode = {
    registerSession: 1,
    declareRequest: 2,
    runRequest: 3,
    declareResponse: 4,
    runResponse: 5,
    declareTool: 6,
    runTool: 7,
    close: 8,
} as const;

type SessionState =
    | "request"
    | "response"
    | "tool"
    | "finished"
    | "closed";

type RuntimeEvent = [
    sequence: number | bigint,
    timestampMs: number | bigint,
    packageName: string,
    level: number,
    type: string,
    references: unknown[],
    data: unknown,
];

type CommandFinishedData = {
    command_id: number | bigint;
    opcode: number;
    command: string;
    state: SessionState;
    result: unknown;
};

type CliConfig = {
    api_key: string;
    provider: string;
    endpoint: string;
    model_id: string;
    context_limit: number;
    compact_threshold: number;
    history?: unknown[] | string;
    session_current: unknown | string;
    tool_definitions: unknown[] | string;
    compaction_prompt_path: string;
    workspace_path: string;
    tool_runtime_executable: string;
    tool_result_timeout_ms?: number;
    session_timeout_ms?: number;
    refresh_workspace?: boolean;
    sandbox_config: {
        read_only?: string[];
        read_write?: string[];
        network: "none" | "internet_client";
    };
};

const testsDirectory = dirname(fileURLToPath(import.meta.url));
const repositoryRoot = resolve(testsDirectory, "..");
const runtimeExecutable = resolve(
    repositoryRoot,
    "executable",
    "session_runtime.exe",
);
const configPath = resolve(testsDirectory, "session.json");
const pipeName =
    `hh-session-runtime-${process.pid}-${Date.now()}`;

if (!existsSync(runtimeExecutable)) {
    throw new Error(
        `session_runtime.exe not found: ${runtimeExecutable}`,
    );
}

function dumpJson(value: unknown): string {
    return JSON.stringify(
        value,
        (_, current: unknown) => {
            if (typeof current !== "bigint") {
                return current;
            }

            if (
                current >= BigInt(Number.MIN_SAFE_INTEGER) &&
                current <= BigInt(Number.MAX_SAFE_INTEGER)
            ) {
                return Number(current);
            }

            return current.toString();
        },
        4,
    );
}

async function loadJson(path: string): Promise<unknown> {
    return JSON.parse(await readFile(path, "utf8"));
}

async function jsonValueOrFile(value: unknown): Promise<unknown> {
    if (typeof value !== "string") {
        return value;
    }

    return loadJson(value);
}

async function loadRuntimeConfig(): Promise<Record<string, unknown>> {
    const input = await loadJson(configPath) as CliConfig;

    return {
        api_key_raw: input.api_key,
        history: await jsonValueOrFile(input.history ?? []),
        session_current: await jsonValueOrFile(input.session_current),
        tool_definitions: await jsonValueOrFile(input.tool_definitions),
        provider: input.provider,
        endpoint: input.endpoint,
        model_id: input.model_id,
        context_limit: input.context_limit,
        compact_threshold: input.compact_threshold,
        tool_result_timeout_ms: input.tool_result_timeout_ms ?? -1,
        session_timeout_ms: input.session_timeout_ms ?? -1,
        compaction_prompt: await readFile(
            input.compaction_prompt_path,
            "utf8",
        ),
        workspace_path: input.workspace_path,
        tool_runtime_executable: input.tool_runtime_executable,
        sandbox_config: {
            read_only: input.sandbox_config.read_only ?? [],
            read_write: input.sandbox_config.read_write ?? [],
            network: input.sandbox_config.network,
        },
        refresh_workspace: input.refresh_workspace ?? false,
    };
}

function asRuntimeEvent(value: unknown): RuntimeEvent {
    if (!Array.isArray(value) || value.length !== 7) {
        throw new Error("invalid EventPort wire event");
    }

    return value as RuntimeEvent;
}

function sameCommandId(
    actual: number | bigint,
    expected: number,
): boolean {
    return BigInt(actual) === BigInt(expected);
}

async function runSession(
    connection: IpcConnection,
    config: Record<string, unknown>,
): Promise<void> {
    let nextCommandId = 1;

    const command = async (
        commandOpcode: number,
        payload?: unknown,
    ): Promise<CommandFinishedData> => {
        const commandId = nextCommandId++;
        const frame = payload === undefined
            ? [commandId, commandOpcode]
            : [commandId, commandOpcode, payload];

        await connection.send(frame);

        for (;;) {
            const raw = await connection.receive();
            if (raw === null) {
                throw new Error(
                    `session_runtime closed while waiting for command ${commandId}`,
                );
            }

            console.log(dumpJson(raw));

            const event = asRuntimeEvent(raw);
            const packageName = event[2];
            const type = event[4];
            const data = event[6];

            if (
                packageName !== "session_runtime" ||
                (type !== "command_finished" && type !== "command_failed")
            ) {
                continue;
            }

            if (
                typeof data !== "object" ||
                data === null ||
                !("command_id" in data)
            ) {
                continue;
            }

            const commandData = data as {
                command_id: number | bigint;
                [key: string]: unknown;
            };

            if (!sameCommandId(commandData.command_id, commandId)) {
                continue;
            }

            if (type === "command_failed") {
                throw new Error(
                    `session_runtime command_failed:\n${dumpJson(data)}`,
                );
            }

            return data as CommandFinishedData;
        }
    };

    let state = (
        await command(opcode.registerSession, config)
    ).state;

    while (state !== "finished") {
        switch (state) {
            case "request":
                await command(opcode.declareRequest);
                state = (await command(opcode.runRequest)).state;
                break;

            case "response":
                await command(opcode.declareResponse);
                state = (await command(opcode.runResponse)).state;
                break;

            case "tool":
                await command(opcode.declareTool);
                state = (await command(opcode.runTool)).state;
                break;

            case "closed":
                throw new Error(
                    "session_runtime entered closed state before finished",
                );
        }
    }

    const closed = await command(opcode.close);
    if (closed.state !== "closed") {
        throw new Error(
            `close returned unexpected state: ${closed.state}`,
        );
    }
}

const server = await listen(pipeName);
const runtime = spawn(
    runtimeExecutable,
    [pipeName],
    {
        cwd: repositoryRoot,
        stdio: [
            "ignore",
            "inherit",
            "inherit",
        ],
        detached: true,
        windowsHide: true,
    },
);

let connection: IpcConnection | undefined;
let shuttingDown = false;
let serverClosing: Promise<void> | undefined;

function closeServer(): Promise<void> {
    serverClosing ??= server.close();
    return serverClosing;
}

function shutdown(): void {
    if (shuttingDown) {
        return;
    }

    shuttingDown = true;
    connection?.closeTransport();
    void closeServer().catch(() => undefined);
}

process.once("SIGINT", shutdown);
process.once("SIGTERM", shutdown);

const runtimeExitBeforeConnect = new Promise<never>((_, reject) => {
    runtime.once("error", reject);
    runtime.once("exit", (code, signal) => {
        reject(
            new Error(
                `session_runtime exited before IPC connection: code=${code} signal=${signal}`,
            ),
        );
    });
});

try {
    connection = await Promise.race([
        server.accept(),
        runtimeExitBeforeConnect,
    ]);
    void closeServer();

    const config = await loadRuntimeConfig();
    await runSession(connection, config);
} finally {
    shutdown();
}

const exit = await new Promise<{
    code: number | null;
    signal: NodeJS.Signals | null;
}>((resolveExit) => {
    if (runtime.exitCode !== null || runtime.signalCode !== null) {
        resolveExit({
            code: runtime.exitCode,
            signal: runtime.signalCode,
        });
        return;
    }

    runtime.once("exit", (code, signal) => {
        resolveExit({ code, signal });
    });
});

console.error(
    `session_runtime exited: code=${exit.code} signal=${exit.signal}`,
);

if (exit.code !== 0) {
    process.exitCode = exit.code ?? 1;
}
