import assert from "node:assert/strict";
import test from "node:test";
import { is_hh_error, normalize_error, PluginErrorQueue } from "@hh/plugin-loader";
import { assertHHError, field } from "./error_helpers.mjs";

const context = { source: "plugin_loader", operation: "invoke" };

function providerError() {
    return {
        source: "provider", operation: "request", type: "http_error",
        message: "HTTP request failed with status 429",
        data: [{ status_code: 429, body: { errors: ["rate limited"] }, path: "/request" },
            null, 5, false, "payload", ["nested"]],
        causes: [],
    };
}

test("normalizing HHError forwards its exact identity and complete open payload", () => {
    const original = providerError();
    assert.equal(normalize_error(original, { ...context, data: [{ pluginId: "example" }] }), original);
    assertHHError(original);
    assert.equal(new PluginErrorQueue().push(original), original);
    for (const invalid of [
        { ...original, code: 429 }, { ...original, data: {} },
        { ...original, causes: [new Error("raw")] }, { ...original, causes: new Array(1) },
        { ...original, message: undefined },
    ]) assert.equal(is_hh_error(invalid), false);
});

test("native causes retain structured lower errors and native metadata", () => {
    const original = providerError();
    const native = new Error("request failed", { cause: original });
    native.code = "PLUGIN_REQUEST_FAILED";
    native.path = "D:\\plugins\\entry.mjs";
    native.details = { retries: 3, body: [1, { field: "value" }] };
    const normalized = assertHHError(normalize_error(native, context));
    assert.equal(normalized.message, native.message);
    assert.equal(field(normalized, "code"), native.code);
    assert.equal(field(normalized, "path"), native.path);
    assert.equal(field(normalized, "stack"), native.stack);
    assert.deepEqual(field(normalized, "details"), native.details);
    assert.equal(normalized.causes.length, 1);
    assert.equal(normalized.causes[0], original);
    assert.equal(field(normalized, "cause"), undefined);
    const upper = normalize_error(new Error("initialization failed", { cause: native }), context);
    assert.equal(upper.causes[0].causes[0], original);
    assertHHError(upper);
});

test("AggregateError keeps each independent cause rather than joining messages", () => {
    const provider = providerError();
    const native = Object.assign(new Error("filesystem failed"), { code: "EACCES", errno: -13, syscall: "open" });
    const aggregate = assertHHError(normalize_error(new AggregateError([provider, native], "two failures"), context));
    assert.equal(aggregate.type, "aggregate_error");
    assert.equal(aggregate.causes.length, 2);
    assert.equal(aggregate.causes[0], provider);
    assert.equal(aggregate.causes[1].type, "system_error");
    assert.equal(field(aggregate.causes[1], "code"), "EACCES");
    assert.equal(field(aggregate, "errors"), undefined);
});

test("arbitrary structured throws and non-JSON values retain explicit data", () => {
    const payload = {
        code: 7, body: { nested: [null, false, { message: "detail" }] },
        id: 18446744073709551615n, missing: undefined, nonFinite: Infinity,
    };
    const normalized = assertHHError(normalize_error(payload, context));
    assert.deepEqual(field(normalized, "body"), payload.body);
    assert.deepEqual(field(normalized, "id"), { value_type: "bigint", value: "18446744073709551615" });
    assert.deepEqual(field(normalized, "missing"), { value_type: "undefined" });
    assert.deepEqual(field(normalized, "nonFinite"), { value_type: "number", value: "Infinity" });
    assert.deepEqual(normalize_error(new Map([["plugin", 3n]]), context).data[0],
        { value_type: "Map", entries: [["plugin", { value_type: "bigint", value: "3" }]] });
    assert.deepEqual(normalize_error(new Set([1, 2]), context).data[0], { value_type: "Set", values: [1, 2] });
    assert.deepEqual(normalize_error(new Date("2026-09-30T00:00:00Z"), context).data[0],
        { value_type: "Date", value: "2026-09-30T00:00:00.000Z" });
});

test("cyclic native errors stay serializable and error capture does not invoke getters", () => {
    const native = new Error("cycle");
    native.cause = native;
    native.details = { name: "node" };
    native.details.self = native.details;
    let getterCalls = 0;
    Object.defineProperty(native, "secret", { get() { getterCalls++; throw new Error("must not run"); } });
    const normalized = assertHHError(normalize_error(native, context));
    assert.equal(getterCalls, 0);
    assert.deepEqual(normalized.causes, []);
    assert.ok(normalized.data.some((item) => item?.field === "cause" && item.$ref === "$"));
    assert.deepEqual(field(normalized, "details").self, { $ref: "$" });
    assert.equal(field(normalized, "secret").value_type, "accessor");
});

test("metadata inspection failures are captured with their original details", () => {
    const inspection = Object.assign(new Error("metadata unavailable"), { code: "CAPTURE_DENIED" });
    const inaccessible = new Proxy({}, { ownKeys() { throw inspection; } });
    const normalized = assertHHError(normalize_error(inaccessible, context));
    assert.equal(normalized.causes.length, 1);
    assert.equal(normalized.causes[0].message, inspection.message);
    assert.equal(field(normalized.causes[0], "code"), "CAPTURE_DENIED");
    let first;
    const second = new Proxy({}, { ownKeys() { throw first; } });
    first = new Proxy({}, { ownKeys() { throw second; } });
    assertHHError(normalize_error(first, context));
});
