import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import test from "node:test";
import { get_error, PluginErrorQueue, PluginRegistry } from "@hh/plugin-loader";
import { plugin as loaderPlugin } from "../dist/api.js";
import { assertHHError, field } from "./error_helpers.mjs";

const manifestPath = fileURLToPath(new URL("../plugin.json", import.meta.url));

async function errorApi() {
    const result = await new PluginRegistry().load(manifestPath);
    assert.equal(result.error, null);
    const api = result.value.apis.find((api) => api.name === "get_error");
    assert.ok(api);
    return api;
}

test("direct loader get_error still returns the original result error reference", async () => {
    const failed = await new PluginRegistry().load(manifestPath + ".missing");
    assert.equal(failed.error.operation, "manifest_read");
    assert.equal(get_error({ result: failed }), failed.error);
    assert.equal(get_error({ result: { value: 1, error: null } }), null);
});

test("manifest-discovered get_error drains the plugin-owned error queue", async () => {
    const api = await errorApi();
    assert.deepEqual(await api.invoke({}), { value: [], error: null });

    let thrown;
    try {
        await loaderPlugin.invoke({ api: 999, input: null });
    } catch (cause) {
        thrown = cause;
    }
    assertHHError(thrown);
    assert.equal(thrown.type, "protocol_error");
    assert.equal(field(thrown, "code"), "api_not_found");
    assert.equal(field(thrown, "apiId"), 999);

    const retrieved = await api.invoke({});
    assert.equal(retrieved.error, null);
    assert.equal(retrieved.value.length, 1);
    assert.equal(retrieved.value[0], thrown);
    assert.deepEqual(await api.invoke({}), { value: [], error: null });
});

test("PluginErrorQueue normalizes native errors and preserves HHError references in FIFO order", () => {
    const queue = new PluginErrorQueue({ source: "example_plugin", operation: "request" });
    const causes = [new Error("raw"), undefined, null, 17n, "raw", [{ keyword: "type" }]];
    const errors = causes.map((cause) => queue.push(cause));
    assert.equal(queue.size, causes.length);
    const existing = errors[0];
    assert.equal(queue.push(existing), existing);
    const drained = queue.drain();
    assert.equal(drained.length, causes.length + 1);
    for (let index = 0; index < causes.length; ++index) {
        assert.equal(assertHHError(drained[index]), errors[index]);
        assert.equal(drained[index].source, "example_plugin");
        assert.equal(drained[index].operation, "request");
    }
    assert.equal(drained.at(-1), existing);
    assert.equal(field(drained[0], "stack"), causes[0].stack);
    assert.deepEqual(drained[1].data[0], { value_type: "undefined" });
    assert.equal(drained[2].data[0], null);
    assert.deepEqual(drained[3].data[0], { value_type: "bigint", value: "17" });
    assert.equal(drained[4].data[0], "raw");
    assert.deepEqual(drained[5].data[0], [{ keyword: "type" }]);
    assert.equal(queue.size, 0);
    assert.deepEqual(queue.drain(), []);
});
