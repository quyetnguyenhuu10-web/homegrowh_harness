import assert from "node:assert/strict";
import test from "node:test";
import { decode, encode } from "../dist/codec.js";
import { isHHError, normalizeError } from "../dist/error.js";
import { errorOf, nativeError, valueOf } from "./support.mjs";

const lowerError = {
    source: "provider",
    operation: "request",
    type: "http_error",
    message: "original provider message",
    data: [{ status_code: 429, body: { retry: true } }, null, [1, "payload"]],
    causes: [],
};

test("HHError is forwarded by identity, including its original context and payload", () => {
    assert.equal(normalizeError(lowerError, "send", "dependency_error"), lowerError);
    assert.ok(isHHError(lowerError));
    assert.equal(isHHError({ ...lowerError, data: {} }), false);
    assert.equal(isHHError({ ...lowerError, causes: [new Error("unconverted")] }), false);
});

test("native codes, path, stack and structured details survive JSON and CBOR", () => {
    const original = nativeError();
    const error = normalizeError(original, "connect", "system_error", [{ api: "net.Socket.connect" }]);
    errorOf({ value: null, error });
    assert.equal(error.message, original.message);
    assert.deepEqual(error.causes, []);
    const properties = error.data.find((entry) => entry?.code === original.code);
    assert.equal(properties.errno, original.errno);
    assert.equal(properties.syscall, original.syscall);
    assert.equal(properties.path, original.path);
    assert.equal(properties.stack, original.stack);
    assert.equal(properties.name, "Error");
    assert.equal(properties.detail, original.detail);
    assert.deepEqual(JSON.parse(JSON.stringify(error)), error);
    assert.deepEqual(valueOf(decode(valueOf(encode(error)))), error);
});

test("native cause chains use causes and preserve an existing HHError leaf", () => {
    const second = new Error("second layer", { cause: lowerError });
    const third = new Error("third layer", { cause: second });
    const error = normalizeError(third, "invoke", "dependency_error");
    assert.equal(error.message, "third layer");
    assert.equal(error.causes.length, 1);
    assert.equal(error.causes[0].message, "second layer");
    assert.equal(error.causes[0].causes[0], lowerError);
    assert.ok(error.data.every((entry) => !Object.hasOwn(entry, "cause")));
    assert.deepEqual(valueOf(decode(valueOf(encode(error)))), error);
});

test("AggregateError keeps independent failures as distinct causes", () => {
    const original = nativeError("independent native failure");
    const error = normalizeError(new AggregateError([original, lowerError], "two failures"), "close", "dependency_error");
    assert.equal(error.causes.length, 2);
    assert.equal(error.causes[0].message, original.message);
    assert.equal(error.causes[1], lowerError);
    assert.ok(error.data.every((entry) => !Object.hasOwn(entry, "errors")));
});

test("non-Error structured failures retain their payload without string flattening", () => {
    const original = { message: "plugin failed", plugin_id: "example", body: { problems: [1, 2] } };
    const error = normalizeError(original, "invoke", "dependency_error");
    assert.deepEqual(error.data, [original]);
    assert.equal(error.message, original.message);
    assert.deepEqual(normalizeError("literal failure", "invoke", "dependency_error").data, ["literal failure"]);
});

test("top-level native codes are normalized into data rather than mistaken for HHError", () => {
    const original = { ...lowerError, code: 5 };
    assert.equal(isHHError(original), false);
    const error = normalizeError(original, "invoke", "system_error");
    errorOf({ value: null, error });
    assert.ok(!Object.hasOwn(error, "code"));
    assert.equal(error.data[0].code, 5);
});

test("a failing native accessor is captured as a cause without escaping normalization", () => {
    const original = nativeError("accessor failed");
    const value = {};
    Object.defineProperty(value, "source", { get() { throw original; } });
    const error = normalizeError(value, "invoke", "dependency_error");
    assert.equal(error.causes.length, 1);
    assert.equal(error.causes[0].message, original.message);
    assert.ok(error.causes[0].data.some((entry) => entry?.code === original.code));
});

test("circular native causes do not create an unserializable causes tree", () => {
    const original = nativeError();
    original.cause = original;
    const error = normalizeError(original, "invoke", "dependency_error");
    assert.ok(isHHError(error));
    assert.doesNotThrow(() => JSON.stringify(error));
    assert.equal(error.causes[0].data[0].circular_reference, true);
});

test("malformed CBOR returns its original decoder details in a protocol error", () => {
    const error = errorOf(decode(Uint8Array.of(0x1a, 0x00)));
    assert.equal(error.source, "ipc_client");
    assert.equal(error.operation, "decode");
    assert.equal(error.type, "protocol_error");
    assert.ok(error.data.some((entry) => entry?.api === "cbor-x.Decoder.decode" && entry.frame_size === 2));
    assert.ok(error.data.some((entry) => typeof entry?.stack === "string"));
});

test("invalid runtime decoder input cannot throw a second error while capturing the first", () => {
    const error = errorOf(decode(undefined));
    assert.equal(error.operation, "decode");
    assert.equal(error.type, "protocol_error");
    assert.ok(error.data.some((entry) => entry?.name === "TypeError" && typeof entry.stack === "string"));
});

test("encoder failures preserve an existing HHError thrown by a payload accessor", () => {
    const value = {};
    Object.defineProperty(value, "payload", {
        enumerable: true,
        get() { throw lowerError; },
    });
    assert.equal(errorOf(encode(value)), lowerError);
});

test("native encoder failures retain structured original properties", () => {
    const original = nativeError("payload accessor failed");
    const value = {};
    Object.defineProperty(value, "payload", {
        enumerable: true,
        get() { throw original; },
    });
    const error = errorOf(encode(value));
    assert.equal(error.operation, "encode");
    assert.equal(error.message, original.message);
    assert.ok(error.data.some((entry) => entry?.path === original.path && entry.stack === original.stack));
});
