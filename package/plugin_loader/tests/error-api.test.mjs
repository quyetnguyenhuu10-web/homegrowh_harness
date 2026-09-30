import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { get_error, PluginRegistry } from "@hh/plugin-loader";

const manifestPath = fileURLToPath(new URL("../plugin.json", import.meta.url));

async function errorApi() {
    const result = await new PluginRegistry().load(manifestPath);
    assert.equal(result.error, null);
    const api = result.value.apis.find((api) => api.name === "get_error");
    assert.ok(api);
    return api;
}

test("manifest-discovered get_error returns the original error reference", async () => {
    const api = await errorApi();
    const failed = await new PluginRegistry().load(manifestPath + ".missing");
    assert.equal(failed.error.operation, "manifest_read");
    assert.equal(get_error({ result: failed }), failed.error);
    const retrieved = await api.invoke({ result: failed });
    assert.equal(retrieved.error, null);
    assert.equal(retrieved.value, failed.error);
    assert.equal(retrieved.value.cause, failed.error.cause);
    assert.equal(retrieved.value.cause.code, "ENOENT");
});

test("get_error supports loadAll results and success", async () => {
    const api = await errorApi();
    const failed = await new PluginRegistry().loadAll(manifestPath + ".missing");
    const retrieved = await api.invoke({ result: failed });
    assert.equal(retrieved.error, null);
    assert.equal(retrieved.value, failed.error);
    const success = { value: undefined, error: null };
    assert.equal(get_error({ result: success }), null);
    assert.deepEqual(await api.invoke({ result: success }), { value: null, error: null });
});

test("get_error preserves arbitrary raw causes and does not consume or overwrite errors", async () => {
    const api = await errorApi();
    const causes = [new Error("raw"), undefined, null, 17n, "raw", [{ keyword: "type" }]];
    const sources = causes.map((cause) => ({
        value: null,
        error: { operation: "invoke", cause, pluginId: "source", apiId: 1 },
    }));
    const results = await Promise.all(sources.map((result) => api.invoke({ result })));
    for (const [index, retrieved] of results.entries()) {
        assert.equal(retrieved.error, null);
        assert.equal(retrieved.value, sources[index].error);
        assert.equal(retrieved.value.cause, causes[index]);
    }
    const reread = await api.invoke({ result: sources[0] });
    assert.equal(reread.value, sources[0].error);
});

test("get_error rejects input outside the declared contract via loader validation", async () => {
    const api = await errorApi();
    for (const input of [
        {}, { result: {} }, { result: { error: "bad" } },
        { result: { error: { operation: "unknown" } } },
    ]) {
        const result = await api.invoke(input);
        assert.equal(result.error.operation, "input_validate");
        assert.ok(Array.isArray(result.error.cause));
    }
});
