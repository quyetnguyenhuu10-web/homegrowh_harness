import { readFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import {
    type PluginHandle,
    PluginRegistry,
} from "@hh/plugin-loader";

type SessionState =
    | "request"
    | "response"
    | "tool"
    | "finished"
    | "closed";

type RuntimeHandle = {
    runtime_id: number | bigint;
};

type CommandResult = {
    command_id: number | bigint;
    state: SessionState | "unregistered";
    result: unknown;
};

type RuntimeCloseResult = {
    code: number | null;
    signal: string | null;
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
    session_runtime_executable: string;
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
const sessionApiManifest = resolve(
    repositoryRoot,
    "plugins",
    "session-api",
    "plugin.json",
);
const configPath = resolve(testsDirectory, "session.json");

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

async function pluginCall<T>(
    plugin: PluginHandle,
    name: string,
    ...args: unknown[]
): Promise<T> {
    const result = await plugin.call(name, ...args);
    if (result.error !== null) {
        const queued = await plugin.call("get_error");
        if (queued.error === null && Array.isArray(queued.value)) {
            for (const error of queued.value) {
                console.error("[plugin_error]", error);
            }
        }
        throw result.error;
    }
    return result.value as T;
}

async function streamEvents(
    sessionApi: PluginHandle,
    runtimeId: number | bigint,
): Promise<void> {
    for (;;) {
        const raw = await pluginCall<unknown | null>(
            sessionApi,
            "stream_event",
            runtimeId,
        );
        if (raw === null) {
            return;
        }

        const event = await pluginCall<unknown>(
            sessionApi,
            "parse_event",
            raw,
        );
        console.log(dumpJson(event));
    }
}

async function streamErrors(
    sessionApi: PluginHandle,
    runtimeId: number | bigint,
): Promise<void> {
    for (;;) {
        const raw = await pluginCall<unknown | null>(
            sessionApi,
            "stream_error",
            runtimeId,
        );
        if (raw === null) {
            return;
        }

        const event = await pluginCall<unknown>(
            sessionApi,
            "parse_event",
            raw,
        );
        console.error("[runtime_error]", dumpJson(event));
    }
}

async function runSession(
    sessionApi: PluginHandle,
    runtimeId: number | bigint,
    config: Record<string, unknown>,
): Promise<void> {
    let nextCommandId = 1;

    const command = async (
        apiName: string,
        ...args: unknown[]
    ): Promise<CommandResult> => {
        const commandId = nextCommandId++;
        const result = await pluginCall<CommandResult>(
            sessionApi,
            apiName,
            runtimeId,
            commandId,
            ...args,
        );
        return result;
    };

    let state = (
        await command("register_session", config)
    ).state as SessionState;

    while (state !== "finished") {
        switch (state) {
            case "request":
                await command("declare_request");
                state = (await command("run_request")).state as SessionState;
                break;

            case "response":
                await command("declare_response");
                state = (await command("run_response")).state as SessionState;
                break;

            case "tool":
                await command("declare_tool");
                state = (await command("run_tool")).state as SessionState;
                break;

            case "closed":
                throw new Error(
                    "runtime entered closed state before finished",
                );
        }
    }

    const closed = await command("close");
    if (closed.state !== "closed") {
        throw new Error(
            `close returned unexpected state: ${closed.state}`,
        );
    }
}

const pluginRegistry = new PluginRegistry();
const loadedSessionApi = await pluginRegistry.load(sessionApiManifest);
if (loadedSessionApi.error !== null) {
    throw loadedSessionApi.error;
}

const sessionApi = loadedSessionApi.value;
const cliConfig = await loadJson(configPath) as CliConfig;
const opened = await pluginCall<RuntimeHandle>(
    sessionApi,
    "open_runtime",
    cliConfig.session_runtime_executable,
);
const eventStream = streamEvents(sessionApi, opened.runtime_id);
const errorStream = streamErrors(sessionApi, opened.runtime_id);

let exit: RuntimeCloseResult;
try {
    const config = await loadRuntimeConfig();
    await runSession(sessionApi, opened.runtime_id, config);
} finally {
    exit = await pluginCall<RuntimeCloseResult>(
        sessionApi,
        "close_runtime",
        opened.runtime_id,
    );
}
await eventStream;
await errorStream;

console.error(
    `runtime exited: code=${exit.code} signal=${exit.signal}`,
);

if (exit.code !== 0) {
    process.exitCode = exit.code ?? 1;
}
