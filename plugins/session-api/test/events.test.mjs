import assert from "node:assert/strict";
import test from "node:test";
import { eventError, isErrorEvent, parseEvent } from "../dist/events.js";
import { normalizeError } from "../dist/error.js";
import { errorOf, hhError, nativeError, valueOf, wire } from "./support.mjs";

test("EventPort retains the original error only in data.error with event metadata outside", () => {
    const error = hhError();
    const raw = wire("command_failed", { error }, "sessions", 4);
    const event = valueOf(parseEvent(raw));
    assert.equal(event.data, raw[6]);
    assert.equal(eventError(event), error);
    assert.equal(event.sequence, raw[0]);
    assert.equal(event.timestamp_ms, raw[1]);
    assert.ok(isErrorEvent(event));
    for (const field of ["error", "code", "status_code", "body", "stack", "operation", "raw", "error_source"]) {
        assert.equal(Object.hasOwn(event, field), false);
    }
    assert.deepEqual(event.references, [{ type: "runtime", value: "example" }]);
    assert.equal(Object.hasOwn(error, "primary"), false);
    assert.equal(Object.hasOwn(error, "secondary"), false);
});

for (const [source, type, kind] of [
    ["provider", "http_error", "provider_http_error"],
    ["provider", "failed", "provider_failed"],
    ["sessions", "failed", "session_failed"],
    ["sessions", "secondary_error", "session_secondary_error"],
    ["session_runtime", "runtime_failed", "runtime_failed"],
    ["session_runtime", "protocol_error", "protocol_error"],
]) {
    test(`${source}.${type} retains the same HHError without copying payload fields onto the event`, () => {
        const error = hhError();
        const event = valueOf(parseEvent(wire(type, { error }, source, 4)));
        assert.equal(event.kind, kind);
        assert.equal(eventError(event), error);
        assert.equal(event.status_code, undefined);
    });
}

test("command success and failure retain command correlation and authoritative result/error", () => {
    const result = { original: [1, 2] };
    const successEvent = valueOf(parseEvent(wire("command_finished", { command_id: 12n, state: "ready", result })));
    assert.equal(successEvent.command_id, 12n);
    assert.equal(successEvent.result, result);
    const error = hhError();
    const failedEvent = valueOf(parseEvent(wire("command_failed", { command_id: 12n, error }, "session_runtime", 4)));
    assert.equal(failedEvent.command_id, 12n);
    assert.equal(eventError(failedEvent), error);
});

for (const [label, raw, path] of [
    ["wire length", [], ""],
    ["uint64 overflow", wire().with(0, 1n << 64n), "sequence"],
    ["unsafe integer", wire().with(1, Number.MAX_SAFE_INTEGER + 1), "timestamp_ms"],
    ["reference tuple", wire().with(5, [["runtime"]]), "references[0]"],
    ["legacy provider payload", wire("http_error", { status_code: 429, body: "original" }, "provider", 4), "data.error"],
    ["error data object", wire("runtime_failed", { error: { message: "legacy" } }, "session_runtime", 4), "data.error"],
    ["non-array error data", wire("runtime_failed", { error: { ...hhError(), data: {} } }, "session_runtime", 4), "data.error"],
    ["singular error cause", wire("runtime_failed", { error: { ...hhError(), cause: hhError() } }, "session_runtime", 4), "data.error"],
    ["missing command state", wire("command_finished", { command_id: 1n }), "data.state"],
    ["missing command result", wire("command_finished", { command_id: 1n, state: "ready" }), "data.result"],
    ["missing command id", wire("command_failed", { error: hhError() }, "session_runtime", 4), "data.command_id"],
]) {
    test(`invalid ${label} returns HHError with the original packet and validation location`, () => {
        const error = errorOf(parseEvent(raw));
        assert.equal(error.operation, "parse_event");
        assert.equal(error.type, "protocol_error");
        assert.equal(error.data[0].raw, raw);
        assert.equal(error.data[0].schema_errors[0].path, path);
    });
}

test("native exception details and cause trees remain structured", () => {
    const leaf = hhError();
    const original = new AggregateError([nativeError(), leaf], "multiple original failures");
    const error = normalizeError(original, "open_runtime", "system_error");
    assert.equal(error.causes.length, 2);
    assert.equal(error.causes[1], leaf);
    assert.ok(error.causes[0].data.some(data => data.code === "ENOENT" && typeof data.stack === "string"));
    assert.equal(normalizeError(error, "invoke", "dependency_error"), error);
    assert.deepEqual(JSON.parse(JSON.stringify(error)), error);
});
