import assert from "node:assert/strict";
import { mkdtemp, mkdir, readFile, readdir, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { PluginRegistry } from "@hh/plugin-loader";
import { assertHHError, field } from "./error_helpers.mjs";

function manifest(id = "fixture", input = true, output = true) {
    return {
        id, version: "1.0.0", api_version: 1,
        execution: { mode: "module", runtimes: ["node", "bun"], entry: "entry.mjs" },
        lifecycle: { scope: "host" },
        sandbox: { enabled: false, filesystem: [], network: "none" },
        references: [], data: {},
        apis: [
            { id: 1, name: "echo", input, output },
            {
                id: 2,
                name: "get_error",
                input: {
                    type: "object",
                    additionalProperties: false,
                    required: [],
                    properties: {},
                },
                output: { type: "array", items: true },
            },
        ],
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
    assert.equal(missing.error.type, "system_error");
    assert.equal(field(missing.error, "code"), "ENOENT");
    assert.equal(field(missing.error, "path"), path.join(root, "absent.json"));
    assert.equal(typeof field(missing.error, "errno"), "number");
    assert.equal(field(missing.error, "syscall"), "open");
    assert.equal(field(missing.error, "name"), "Error");
    assert.equal(typeof field(missing.error, "stack"), "string");
    assert.equal(field(missing.error, "manifestPath"), path.join(root, "absent.json"));
    await writeFile(manifestPath, "{");
    const malformed = await new PluginRegistry().load(manifestPath);
    assert.equal(malformed.error.operation, "manifest_parse");
    assert.equal(malformed.error.type, "protocol_error");
    assert.equal(field(malformed.error, "name"), "SyntaxError");
});

test("manifest AJV errors, duplicate API ids/names and unsafe ids are data", async (t) => {
    const invalid = manifest();
    invalid.execution.extra = true;
    const { manifestPath } = await fixture(t, invalid);
    const result = await new PluginRegistry().load(manifestPath);
    assert.equal(result.error.operation, "manifest_validate");
    assert.equal(result.error.type, "validation_error");
    assert.ok(field(result.error, "schema_errors").some((error) => error.keyword === "additionalProperties"));
    assert.equal(field(result.error, "manifestPath"), manifestPath);
    for (const [id, name, code] of [
        [1, "other", "duplicate_api_id"],
        [3, "echo", "duplicate_api_name"],
        [Number.MAX_SAFE_INTEGER + 1, "other", "unsafe_api_id"],
    ]) {
        const value = manifest();
        value.apis.push({ id, name, input: true, output: true });
        const fixtureValue = await fixture(t, value);
        const failed = await new PluginRegistry().load(fixtureValue.manifestPath);
        assert.equal(field(failed.error, "code"), code);
    }
});

test("every plugin must declare the standard get_error API", async (t) => {
    const missing = manifest("missing-get-error");
    missing.apis = missing.apis.filter((api) => api.name !== "get_error");
    const a = await fixture(t, missing);
    const missingResult = await new PluginRegistry().load(a.manifestPath);
    assert.equal(missingResult.error.operation, "manifest_validate");
    assert.equal(field(missingResult.error, "code"), "missing_required_api");
    assert.equal(field(missingResult.error, "value"), "get_error");

    const invalid = manifest("invalid-get-error");
    invalid.apis.find((api) => api.name === "get_error").output = true;
    const b = await fixture(t, invalid);
    const invalidResult = await new PluginRegistry().load(b.manifestPath);
    assert.equal(invalidResult.error.operation, "manifest_validate");
    assert.equal(field(invalidResult.error, "code"), "invalid_required_api_contract");
    assert.equal(field(invalidResult.error, "value"), "get_error");
});

test("version, entry confinement and module failures identify their stage", async (t) => {
    const version = manifest();
    version.api_version = 2;
    const a = await fixture(t, version);
    assert.equal(field((await new PluginRegistry().load(a.manifestPath)).error, "code"),
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
    assert.equal(field(syntax.error, "name"), "SyntaxError");
});

test("schema compilation keeps AJV exception and API context", async (t) => {
    const { manifestPath } = await fixture(t, manifest("schema", { type: "invalid" }));
    const result = await new PluginRegistry().load(manifestPath);
    assert.equal(result.error.operation, "schema_compile");
    assert.equal(field(result.error, "pluginId"), "schema");
    assert.equal(field(result.error, "apiId"), 1);
    assert.equal(field(result.error, "direction"), "input");
    assert.equal(field(result.error, "name"), "Error");
    assert.equal(field(result.error, "manifestPath"), manifestPath);
});

test("input/output validation is distinct and retains native AJV errors", async (t) => {
    const { plugin } = await handle(t, manifest("validation", { type: "integer" }, { type: "string" }));
    const api = plugin.apis[0];
    const input = await api.invoke("bad");
    assert.equal(input.error.operation, "input_validate");
    const savedErrors = field(input.error, "schema_errors");
    const output = await api.invoke(1);
    assert.equal(output.error.operation, "output_validate");
    assert.equal(savedErrors[0].params.type, "integer");
    assert.equal(field(output.error, "schema_errors")[0].params.type, "string");
    assert.equal(field(input.error, "schema_errors"), savedErrors);
    assert.equal(field(input.error, "direction"), "input");
    assert.equal(field(output.error, "direction"), "output");
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
    assert.equal(field(count.error, "code"), "argument_count_mismatch");
    assert.equal(field(count.error, "expected"), 2);
    assert.equal(field(count.error, "actual"), 1);

    const missing = await plugin.call("missing", 7);
    assert.equal(missing.error.operation, "api_resolve");
    assert.equal(field(missing.error, "code"), "api_not_found");
    assert.equal(field(missing.error, "value"), "missing");
});

test("generic call refuses schemas without positional mapping", async (t) => {
    const { plugin } = await handle(t);
    const result = await plugin.call("echo", 1);
    assert.equal(result.error.operation, "argument_map");
    assert.equal(field(result.error, "code"), "positional_mapping_unavailable");
});

test("parallel invocations preserve exception details and primitive throws", async (t) => {
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
    assert.equal(a.error.message, cause.message);
    assert.equal(field(a.error, "stack"), cause.stack);
    assert.equal(field(a.error, "code"), "ORIGINAL_CODE");
    assert.equal(field(a.error, "pluginId"), "fixture");
    assert.equal(field(a.error, "apiId"), 1);
    assert.equal(assertHHError(b.error).data[0], "raw primitive");
    assert.deepEqual(assertHHError(c.error).data[0], { value_type: "undefined" });
    assert.deepEqual(d, { value: 42, error: null });
});

test("invoke adds loader context while get_error forwards the original HHError", async (t) => {
    const { plugin } = await handle(t, manifest("error-plugin"), `
        const errors = [];
        export const plugin = { invoke({ api, input }) {
            if (api === 2) return errors.splice(0);
            errors.push(input);
            throw input;
        }};
    `);
    const original = {
        source: "provider", operation: "request", type: "http_error", message: "Rate limited",
        data: [{ status_code: 429, path: "/request", body: { reason: "quota" } }], causes: [],
    };
    const failed = await plugin.apis[0].invoke(original);
    assert.equal(failed.value, null);
    assertHHError(failed.error);
    assert.equal(failed.error.source, "plugin_loader");
    assert.equal(failed.error.type, "dependency_error");
    assert.deepEqual(failed.error.data, [{ pluginId: "error-plugin", apiId: 1 }]);
    assert.equal(failed.error.causes.length, 1);
    assert.equal(failed.error.causes[0], original);
    const retrieved = await plugin.call("get_error");
    assert.equal(retrieved.error, null);
    assert.equal(retrieved.value[0], original);
    assert.deepEqual(await plugin.call("get_error"), { value: [], error: null });
});

test("get_error normalizes native entries before validating an HHError output schema", async (t) => {
    const loaderManifest = JSON.parse(await readFile(new URL("../plugin.json", import.meta.url), "utf8"));
    const value = manifest("native-queue");
    value.apis[1].output = loaderManifest.apis[0].output;
    const { plugin } = await handle(t, value, `
        export const plugin = { invoke() {
            return [Object.assign(new Error("plugin failure"), { code: "PLUGIN_CODE", body: { x: 1 } }), 17n];
        }};
    `);
    const retrieved = await plugin.call("get_error");
    assert.equal(retrieved.error, null);
    assert.equal(retrieved.value.length, 2);
    assert.equal(assertHHError(retrieved.value[0]).source, "native-queue");
    assert.equal(field(retrieved.value[0], "code"), "PLUGIN_CODE");
    assert.deepEqual(field(retrieved.value[0], "body"), { x: 1 });
    assert.deepEqual(assertHHError(retrieved.value[1]).data[0], { value_type: "bigint", value: "17" });
});

test("existing loader context is forwarded without redundant wrapping", async (t) => {
    const { plugin } = await handle(t, manifest(), `
        export const plugin = { invoke({ input }) { throw input; }};
    `);
    const original = {
        source: "plugin_loader", operation: "invoke", type: "protocol_error", message: "Plugin failed",
        data: [{ pluginId: "fixture", apiId: 1, code: "ORIGINAL" }], causes: [],
    };
    const failed = await plugin.apis[0].invoke(original);
    assert.equal(failed.error, original);
    assertHHError(failed.error);
});

test("get_error transfers an already normalized array without copying it", async (t) => {
    const { plugin } = await handle(t, manifest(), `
        let errors = [];
        export const plugin = { invoke({ api, input }) {
            if (api === 2) return errors;
            errors = input;
            return null;
        }};
    `);
    const errors = [{
        source: "plugin", operation: "request", type: "system_error", message: "Failure",
        data: [{ code: 5 }], causes: [],
    }];
    await plugin.apis[0].invoke(errors);
    const retrieved = await plugin.call("get_error");
    assert.equal(retrieved.error, null);
    assert.equal(retrieved.value, errors);
    assert.equal(retrieved.value[0], errors[0]);
});

test("stream_error forwards HHError, Result and EventPort payloads intact", async (t) => {
    const value = manifest("stream-plugin");
    value.apis.push({ id: 3, name: "stream_error", input: true, output: true });
    const { plugin } = await handle(t, value);
    const original = {
        source: "sandbox", operation: "grant_filesystem", type: "system_error",
        message: "SetNamedSecurityInfoW failed",
        data: [{ code: 5, category: "system", api: "SetNamedSecurityInfoW", path: "D:\\tools" }], causes: [],
    };
    const event = {
        package: "sessions", level: "error", type: "command_failed", references: [],
        data: { error: original },
    };
    const result = { value: null, error: original };
    const api = plugin.apis.find((api) => api.name === "stream_error");
    for (const payload of [original, event, result, null]) {
        const output = await api.invoke(payload);
        assert.equal(output.error, null);
        assert.equal(output.value, payload);
    }
    assertHHError(event.data.error);
    assert.equal(result.error, original);
});

test("loadAll returns module import errors instead of treating them as absent manifests", async (t) => {
    const { root } = await fixture(t);
    const directory = path.join(root, "missing-entry");
    await mkdir(directory);
    await writeFile(path.join(directory, "plugin.json"), JSON.stringify(manifest("missing-entry")));
    const loaded = await new PluginRegistry().loadAll(root);
    assert.deepEqual(loaded.plugins, []);
    assert.equal(loaded.error.operation, "module_import");
    assert.equal(field(loaded.error, "code"), "ERR_MODULE_NOT_FOUND");
    assert.equal(field(loaded.error, "pluginId"), "missing-entry");
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
    assert.equal(field(result.error, "code"), "duplicate_plugin_id");
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
    assert.equal(field(absent.error, "code"), "ENOENT");
    assert.equal(field(absent.error, "rootDirectory"), path.join(root, "absent"));
});

test("loadAll surfaces manifest read errors instead of swallowing them", async (t) => {
    const { root } = await fixture(t);
    const directory = path.join(root, "broken");
    await mkdir(directory);
    await mkdir(path.join(directory, "plugin.json"));
    const result = await new PluginRegistry().loadAll(root);
    assert.equal(result.error.operation, "manifest_read");
    assert.equal(field(result.error, "name"), "Error");
    assert.notEqual(field(result.error, "code"), "ENOENT");
});
