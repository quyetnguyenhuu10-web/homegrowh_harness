import assert from "node:assert/strict";
import test from "node:test";
import {
    uint64, registerSession, declareRequest, runRequest, declareResponse,
    runResponse, declareTool, runTool, closeSession,
} from "../dist/commands.js";
import { errorOf, valueOf } from "./support.mjs";

test("all command builders preserve the wire tuples inside Result", () => {
    const builders = [declareRequest, runRequest, declareResponse, runResponse, declareTool, runTool, closeSession];
    builders.forEach((builder, index) => {
        assert.deepEqual(valueOf(builder(42)), [42n, index + 2]);
        assert.deepEqual(valueOf(builder(42n)), [42n, index + 2]);
    });
    const config = { context_limit: 100, compact_threshold: 60n, arbitrary: { original: true } };
    assert.deepEqual(valueOf(registerSession(1, config)), [1n, 1, {
        ...config, context_limit: 100n, compact_threshold: 60n,
    }]);
    assert.equal(valueOf(registerSession(1, { ...config, compact_threshold: 0 }))[2].compact_threshold, 0n);
});

test("uint64 rejects lossy, negative, fractional and out-of-range identifiers", () => {
    assert.equal(valueOf(uint64(0)), 0n);
    assert.equal(valueOf(uint64((1n << 64n) - 1n)), (1n << 64n) - 1n);
    for (const value of [-1, -1n, 0.5, NaN, Infinity, Number.MAX_SAFE_INTEGER + 1, 1n << 64n, "42", null]) {
        const error = errorOf(uint64(value));
        assert.equal(error.type, "validation_error");
        assert.equal(error.operation, "uint64");
    }
    for (const builder of [declareRequest, runRequest, declareResponse, runResponse, declareTool, runTool, closeSession]) {
        assert.equal(errorOf(builder(0)).data[0].field, "command_id");
    }
});

test("register forwards validation context and captures native config accessor errors", () => {
    const config = { context_limit: 100, compact_threshold: -1 };
    assert.equal(errorOf(registerSession(1, config)).data[0].field, "compact_threshold");
    const original = Object.assign(new TypeError("original accessor message"), { code: "ORIGINAL_CODE", path: "original-path" });
    const accessor = { get context_limit() { throw original; } };
    const error = errorOf(registerSession(1, accessor));
    assert.equal(error.message, original.message);
    assert.ok(error.data.some(data => data.code === original.code && data.stack === original.stack));
});
