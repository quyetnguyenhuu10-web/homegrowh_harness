import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { PluginRegistry } from "@hh/plugin-loader";
import { plugin } from "../dist/index.js";
import { errorOf, hhError, valueOf, wire } from "./support.mjs";

test("all 16 plugin API schemas load through the existing plugin loader", async () => {
    const loaded = valueOf(await new PluginRegistry().load(fileURLToPath(new URL("../plugin.json", import.meta.url))));
    assert.equal(loaded.apis.length, 16);
    const parsed = valueOf(await loaded.call("parse_event", wire()));
    assert.equal(valueOf(parsed).kind, "ready");
});

test("plugin forwards parse errors through Result and drains the same HHError objects", async () => {
    await plugin.invoke({ api: 15, input: {} });
    const failed = await plugin.invoke({ api: 13, input: { raw: ["malformed"] } });
    const error = errorOf(failed);
    assert.equal(error.source, "session_api");
    assert.equal(error.operation, "parse_event");
    const errors = await plugin.invoke({ api: 15, input: {} });
    assert.equal(errors.length, 1);
    assert.equal(errors[0], error);
    assert.deepEqual(await plugin.invoke({ api: 15, input: {} }), []);
});

test("unsupported API and malformed invocation input always produce queued HHError", async () => {
    await plugin.invoke({ api: 15 });
    for (const request of [
        { api: 999, input: {} }, { api: 9, input: null }, { api: 3, input: {} }, null,
    ]) {
        const error = errorOf(await plugin.invoke(request));
        assert.equal(error.source, "session_api");
        assert.equal(error.type, "validation_error");
    }
    const errors = await plugin.invoke({ api: 15 });
    assert.equal(errors.length, 4);
    for (const error of errors) errorOf({ value: null, error });
});

test("parsed events retain provider and secondary payloads in data.error", async () => {
    const original = hhError();
    for (const [source, type] of [["provider", "http_error"], ["sessions", "secondary_error"]]) {
        const parsed = valueOf(await plugin.invoke({ api: 13, input: { raw: wire(type, { error: original }, source, 4) } }));
        assert.equal(parsed.data.error, original);
        assert.equal(parsed.error, undefined);
        assert.equal(parsed.code, undefined);
        assert.equal(parsed.body, undefined);
    }
});

test("manifest outputs use one canonical HHError schema and preserve the required get_error contract", async () => {
    const manifest = JSON.parse(await readFile(new URL("../plugin.json", import.meta.url), "utf8"));
    const canonical = manifest.apis[0].output.$defs.hh_error;
    assert.deepEqual(canonical.required, ["source", "operation", "type", "message", "data", "causes"]);
    assert.equal(canonical.properties.data.type, "array");
    assert.equal(canonical.properties.causes.type, "array");
    for (const api of manifest.apis) assert.deepEqual(api.output.$defs.hh_error, canonical);
    const getError = manifest.apis.find(api => api.id === 15);
    assert.equal(getError.output.type, "array");
    assert.equal(getError.output.items, true);
    assert.deepEqual(getError.output.allOf[0].items, { $ref: "#/$defs/hh_error" });
});
