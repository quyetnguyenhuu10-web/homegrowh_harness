import assert from "node:assert/strict";
import test from "node:test";
import childProcess from "node:child_process";
import { syncBuiltinESMExports } from "node:module";
import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
import path from "node:path";
import { PluginRegistry } from "@hh/plugin-loader";
import { plugin } from "../dist/index.js";
import { errorOf, valueOf } from "./support.mjs";

const fixturePath = fileURLToPath(new URL("./fixtures/runtime.mjs", import.meta.url));

function fixtureSpawn(t, args) {
    const spawn = childProcess.spawn;
    t.mock.method(childProcess, "spawn", (_executable, originalArgs, options) => spawn(
        process.execPath, args(originalArgs), options,
    ));
    syncBuiltinESMExports();
    t.after(() => {
        t.mock.restoreAll();
        syncBuiltinESMExports();
    });
}

test("real child process and IPC preserve command errors, get_error identity and independent streams", { timeout: 15000 }, async (t) => {
    fixtureSpawn(t, args => [fixturePath, ...args]);
    const loaded = valueOf(await new PluginRegistry().load(fileURLToPath(new URL("../plugin.json", import.meta.url))));
    await loaded.call("get_error");
    const opened = valueOf(valueOf(await loaded.call("open_runtime", "fixture-runtime")));
    const id = opened.runtime_id;
    let closed = false;
    t.after(async () => {
        if (!closed) await plugin.invoke({ api: 12, input: { runtime_id: id } });
    });
    assert.equal(valueOf(valueOf(await loaded.call("receive", id))).kind, "ready");
    const config = {
        api_key_raw: "fixture-key", history: [], session_current: { messages: [{ role: "user", content: "fixture" }] }, tool_definitions: [],
        provider: "openai", endpoint: "https://example.test", model_id: "fixture-model",
        context_limit: 100n, compact_threshold: 60n, tool_result_timeout_ms: 1000,
        session_timeout_ms: 1000, compaction_prompt: "", workspace_path: process.cwd(),
        tool_runtime_executable: process.execPath,
        sandbox_config: { read_only: [], read_write: [], network: "none" }, refresh_workspace: false,
    };
    assert.equal(valueOf(valueOf(await loaded.call("register_session", id, 1n, config))).result.kind, 1);
    const commands = [
        ["declare_request", 2], ["declare_response", 4], ["run_response", 5],
        ["declare_tool", 6], ["run_tool", 7],
    ];
    let commandId = 2n;
    for (const [name, kind] of commands) {
        const result = valueOf(valueOf(await loaded.call(name, id, commandId++)));
        assert.equal(result.result.kind, kind);
    }

    const failed = valueOf(await loaded.call("run_request", id, commandId));
    const primary = errorOf(failed);
    assert.equal(primary.source, "sessions");
    assert.equal(primary.operation, "run_request");
    assert.equal(primary.causes[0].source, "provider");
    assert.equal(primary.causes[0].data[0].status_code, 429);
    assert.deepEqual(primary.causes[0].data[0].body, { original: ["provider", null, true] });
    assert.deepEqual(primary.data, [null, true, 12, "text", [1, { arbitrary: "JSON" }]]);
    assert.equal(valueOf(valueOf(await loaded.call("stream_error", id))), primary);
    const secondary = valueOf(valueOf(await loaded.call("stream_error", id)));
    assert.equal(secondary.source, "sandbox");
    assert.equal(secondary.operation, "release");
    assert.equal(secondary.data[0].code, 5);
    assert.equal(Object.hasOwn(secondary, "secondary"), false);
    const queued = valueOf(await loaded.call("get_error"));
    assert.equal(queued.length, 1);
    assert.equal(queued[0], primary);
    assert.deepEqual(valueOf(await loaded.call("get_error")), []);

    const events = [];
    for (let index = 0; index < commands.length + 3; index++) {
        events.push(valueOf(valueOf(await loaded.call("stream_event", id))));
    }
    const commandFailed = events.find(event => event[4] === "command_failed");
    const secondaryEvent = events.find(event => event[4] === "secondary_error");
    assert.equal(commandFailed[6].error, primary);
    assert.equal(secondaryEvent[6].error, secondary);
    assert.equal(secondaryEvent[2], "sessions");
    assert.equal(secondaryEvent[3], 4);
    assert.ok(secondaryEvent[0] > commandFailed[0]);

    const result = valueOf(valueOf(await loaded.call("close_runtime", id)));
    assert.deepEqual(result, { code: 0, signal: null });
    closed = true;
});

test("real missing executable returns original spawn code, path and arguments through Result/get_error", { timeout: 10000 }, async () => {
    await plugin.invoke({ api: 15 });
    const executable = path.join(path.dirname(fixturePath), `missing-${randomUUID()}.exe`);
    const error = errorOf(await plugin.invoke({ api: 9, input: { executable } }));
    assert.equal(error.source, "session_api");
    assert.equal(error.operation, "open_runtime");
    assert.equal(error.type, "system_error");
    const native = error.data.find(data => data?.code === "ENOENT");
    assert.ok(native);
    assert.equal(native.path, executable);
    assert.equal(typeof native.errno, "number");
    assert.equal(typeof native.stack, "string");
    assert.equal(native.spawnargs.length, 1);
    const queued = await plugin.invoke({ api: 15 });
    assert.equal(queued[0], error);
    assert.deepEqual(JSON.parse(JSON.stringify(error)), error);
});

test("real exit before IPC connection reports its exact exit status and releases the listener", { timeout: 10000 }, async (t) => {
    fixtureSpawn(t, () => ["-e", "process.exitCode = 23"]);
    const error = errorOf(await plugin.invoke({ api: 9, input: { executable: "exit-fixture" } }));
    assert.equal(error.operation, "open_runtime");
    assert.equal(error.type, "state_error");
    assert.equal(error.data[0].exit_code, 23);
    await plugin.invoke({ api: 15 });
});
