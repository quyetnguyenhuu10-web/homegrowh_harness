import assert from "node:assert/strict";
import { mkdtemp, mkdir, readdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { PluginRegistry } from "@hh/plugin-loader";

function manifest(id = "fixture", input = true, output = true) {
    return {
        id, version: "1.0.0", api_version: 1,
        execution: { mode: "module", runtimes: ["node", "bun"], entry: "entry.mjs" },
        lifecycle: { scope: "host" },
        sandbox: { enabled: false, filesystem: [], network: "none" },
        references: [], data: {},
        apis: [{ id: 1, name: "echo", input, output }],
    };
}

async function fixture(t, value = manifest(), source =
    "export const plugin = { invoke({ input }) { return input; } };") {
    const root = await mkdtemp(path.join(os.tmpdir(), "hh-plugin-loader-"));
    t.after(() => rm(root, { recursive: true, force: true }));
    await writeFile(path.join(root, "entry.mjs"), source);
    const manifestPath = path.join(root, "plugin.json");
    await writeFile(manifestPath, JSON.stringify(value));
    return { root, manifestPath };
}

async function handle(t, value = manifest(), source) {
    const { manifestPath } = await fixture(t, value, source);
    const registry = new PluginRegistry();
    const result = await registry.load(manifestPath);
    assert.equal(result.error, null);
    return { registry, plugin: result.value, manifestPath };
}

test("load, snapshots and null/undefined outputs have separate success state", async (t) => {
    const { plugin } = await handle(t);
    const snapshot = plugin.manifest;
    snapshot.id = "changed";
    assert.equal(plugin.manifest.id, "fixture");
    assert.ok(Object.isFrozen(plugin.apis));
    assert.deepEqual(await plugin.apis[0].invoke(null), { value: null, error: null });
    assert.deepEqual(await plugin.apis[0].invoke(undefined), { value: undefined, error: null });
});

test("read and JSON errors are returned with native details", async (t) => {
    const { root, manifestPath } = await fixture(t);
    const missing = await new PluginRegistry().load(path.join(root, "absent.json"));
    assert.equal(missing.value, null);
    assert.equal(missing.error.operation, "manifest_read");
    assert.equal(missing.error.cause.code, "ENOENT");
    assert.ok(missing.error.cause instanceof Error);
    await writeFile(manifestPath, "{");
    const malformed = await new PluginRegistry().load(manifestPath);
    assert.equal(malformed.error.operation, "manifest_parse");
    assert.ok(malformed.error.cause instanceof SyntaxError);
});

test("manifest AJV errors, duplicate API ids/names and unsafe ids are data", async (t) => {
    const invalid = manifest();
    invalid.execution.extra = true;
    const { manifestPath } = await fixture(t, invalid);
    const result = await new PluginRegistry().load(manifestPath);
    assert.equal(result.error.operation, "manifest_validate");
    assert.ok(result.error.cause.some((error) => error.keyword === "additionalProperties"));
    for (const [id, name, code] of [
        [1, "other", "duplicate_api_id"],
        [2, "echo", "duplicate_api_name"],
        [Number.MAX_SAFE_INTEGER + 1, "other", "unsafe_api_id"],
    ]) {
        const value = manifest();
        value.apis.push({ id, name, input: true, output: true });
        const fixtureValue = await fixture(t, value);
        const failed = await new PluginRegistry().load(fixtureValue.manifestPath);
        assert.equal(failed.error.cause.code, code);
    }
});

test("version, entry confinement and module failures identify their stage", async (t) => {
    const version = manifest();
    version.api_version = 2;
    const a = await fixture(t, version);
    assert.equal((await new PluginRegistry().load(a.manifestPath)).error.cause.code,
        "unsupported_api_version");
    const escape = manifest();
    escape.execution.entry = "../entry.mjs";
    const b = await fixture(t, escape);
    assert.equal((await new PluginRegistry().load(b.manifestPath)).error.operation,
        "module_resolve");
    const c = await fixture(t, manifest(), "export const invalid = true;");
    assert.equal((await new PluginRegistry().load(c.manifestPath)).error.operation,
        "module_validate");
    const d = await fixture(t, manifest(), "export const plugin = ;");
    const syntax = await new PluginRegistry().load(d.manifestPath);
    assert.equal(syntax.error.operation, "module_import");
    assert.ok(syntax.error.cause instanceof SyntaxError);
});

test("schema compilation keeps AJV exception and API context", async (t) => {
    const { manifestPath } = await fixture(t, manifest("schema", { type: "invalid" }));
    const result = await new PluginRegistry().load(manifestPath);
    assert.equal(result.error.operation, "schema_compile");
    assert.equal(result.error.pluginId, "schema");
    assert.equal(result.error.apiId, 1);
    assert.equal(result.error.direction, "input");
    assert.ok(result.error.cause instanceof Error);
});

test("input/output validation is distinct and retains native AJV errors", async (t) => {
    const { plugin } = await handle(t, manifest("validation", { type: "integer" }, { type: "string" }));
    const api = plugin.apis[0];
    const input = await api.invoke("bad");
    assert.equal(input.error.operation, "input_validate");
    const savedErrors = input.error.cause;
    const output = await api.invoke(1);
    assert.equal(output.error.operation, "output_validate");
    assert.equal(savedErrors[0].params.type, "integer");
    assert.equal(output.error.cause[0].params.type, "string");
    assert.equal(input.error.cause, savedErrors);
});

test("generic call maps positional arguments by input.required order", async (t) => {
    const value = manifest("positional", {
        type: "object",
        additionalProperties: false,
        required: ["first", "second"],
        properties: {
            first: { type: "integer" },
            second: { type: "string" },
        },
    }, true);
    const { plugin } = await handle(t, value);

    assert.deepEqual(
        await plugin.call("echo", 7, "value"),
        { value: { first: 7, second: "value" }, error: null },
    );

    const count = await plugin.call("echo", 7);
    assert.equal(count.error.operation, "argument_map");
    assert.deepEqual(count.error.cause, {
        code: "argument_count_mismatch",
        expected: 2,
        actual: 1,
    });

    const missing = await plugin.call("missing", 7);
    assert.equal(missing.error.operation, "api_resolve");
    assert.deepEqual(missing.error.cause, {
        code: "api_not_found",
        value: "missing",
    });
});

test("generic call refuses schemas without positional mapping", async (t) => {
    const { plugin } = await handle(t);
    const result = await plugin.call("echo", 1);
    assert.equal(result.error.operation, "argument_map");
    assert.deepEqual(result.error.cause, {
        code: "positional_mapping_unavailable",
    });
});

test("parallel invocations keep exact thrown objects and primitive causes", async (t) => {
    const { plugin } = await handle(t, manifest(), `
        export const plugin = {
            async invoke({ input }) {
                await Promise.resolve();
                if (input.fail) throw input.cause;
                return input.value;
            }
        };
    `);
    const cause = new Error("native failure");
    cause.code = "ORIGINAL_CODE";
    const [a, b, c, d] = await Promise.all([
        plugin.apis[0].invoke({ fail: true, cause }),
        plugin.apis[0].invoke({ fail: true, cause: "raw primitive" }),
        plugin.apis[0].invoke({ fail: true, cause: undefined }),
        plugin.apis[0].invoke({ value: 42 }),
    ]);
    assert.equal(a.error.operation, "invoke");
    assert.equal(a.error.cause, cause);
    assert.equal(a.error.cause.code, "ORIGINAL_CODE");
    assert.equal(b.error.cause, "raw primitive");
    assert.equal(c.error.cause, undefined);
    assert.deepEqual(d, { value: 42, error: null });
});

test("HH uint64 limits and bigint remain supported", async (t) => {
    const { plugin } = await handle(t, manifest("integer",
        { "x-hh-type": "uint64-positive" }, { "x-hh-type": "uint64-positive" }));
    const api = plugin.apis[0];
    const max = (1n << 64n) - 1n;
    assert.deepEqual(await api.invoke(max), { value: max, error: null });
    for (const value of [0n, -1n, max + 1n, Number.MAX_SAFE_INTEGER + 1]) {
        assert.equal((await api.invoke(value)).error.operation, "input_validate");
    }
});

test("duplicate registration does not replace the existing handle", async (t) => {
    const { registry, plugin, manifestPath } = await handle(t);
    const result = await registry.load(manifestPath);
    assert.equal(result.error.operation, "register");
    assert.equal(result.error.cause.code, "duplicate_plugin_id");
    assert.deepEqual(registry.list(), [plugin]);
});

test("loadAll skips absent manifests, retains partial progress and exposes directory errors", async (t) => {
    const { root } = await fixture(t);
    const registry = new PluginRegistry();
    await mkdir(path.join(root, "empty"));
    const skipped = await registry.loadAll(root);
    assert.deepEqual(skipped, { plugins: [], error: null });

    for (const name of ["one", "two", "three"]) {
        await mkdir(path.join(root, name));
    }
    const names = (await readdir(root, { withFileTypes: true }))
        .filter((entry) => entry.isDirectory() && entry.name !== "empty")
        .map((entry) => entry.name);
    for (const [index, name] of names.entries()) {
        const directory = path.join(root, name);
        await writeFile(path.join(directory, "entry.mjs"),
            "export const plugin = { invoke({input}) { return input; } };");
        await writeFile(path.join(directory, "plugin.json"),
            index === names.length - 1 ? "{" : JSON.stringify(manifest(name)));
    }
    const result = await registry.loadAll(root);
    assert.equal(result.error.operation, "manifest_parse");
    assert.equal(result.plugins.length, 2);
    assert.deepEqual(registry.list(), result.plugins);
    const absent = await registry.loadAll(path.join(root, "absent"));
    assert.equal(absent.error.operation, "directory_read");
    assert.equal(absent.error.cause.code, "ENOENT");
});

test("loadAll surfaces manifest read errors instead of swallowing them", async (t) => {
    const { root } = await fixture(t);
    const directory = path.join(root, "broken");
    await mkdir(directory);
    await mkdir(path.join(directory, "plugin.json"));
    const result = await new PluginRegistry().loadAll(root);
    assert.equal(result.error.operation, "manifest_read");
    assert.ok(result.error.cause instanceof Error);
    assert.notEqual(result.error.cause.code, "ENOENT");
});
