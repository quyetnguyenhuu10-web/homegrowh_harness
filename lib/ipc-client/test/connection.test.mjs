import assert from "node:assert/strict";
import test from "node:test";
import { Connection, maxFrameSize } from "../dist/ipc.js";
import { IpcConnection } from "../dist/index.js";
import { encode } from "../dist/codec.js";
import { errorOf, frame, nativeError, TestSocket, valueOf } from "./support.mjs";

function createConnection() {
    const socket = new TestSocket();
    return { socket, connection: valueOf(Connection.create(socket)) };
}

test("fragmented and coalesced frames preserve framing and empty payloads", async () => {
    const { socket, connection } = createConnection();
    const reading = connection.read();
    const data = Buffer.concat([frame(Buffer.from("first")), frame(Buffer.alloc(0)), frame(Buffer.from("last"))]);
    socket.emit("data", data.subarray(0, 2));
    socket.emit("data", data.subarray(2, 6));
    socket.emit("data", data.subarray(6));
    assert.equal(valueOf(await reading).toString(), "first");
    assert.equal(valueOf(await connection.read()).length, 0);
    assert.equal(valueOf(await connection.read()).toString(), "last");
    socket.emit("end");
    assert.equal(valueOf(await connection.read()), null);
});

test("a concurrent read returns a state error while the first read remains usable", async () => {
    const { socket, connection } = createConnection();
    const first = connection.read();
    assert.equal(errorOf(await connection.read()).type, "state_error");
    socket.emit("data", frame(Buffer.from("complete")));
    assert.equal(valueOf(await first).toString(), "complete");
});

for (const [label, bytes, expectedSize] of [
    ["partial header", Buffer.from([1, 0]), 4],
    ["partial payload", frame(Buffer.from("abc")).subarray(0, 5), 3],
]) {
    test(`${label} at EOF returns a structured protocol error`, async () => {
        const { socket, connection } = createConnection();
        const reading = connection.read();
        socket.emit("data", bytes);
        socket.emit("end");
        const error = errorOf(await reading);
        assert.equal(error.type, "protocol_error");
        assert.equal(error.operation, "read");
        assert.equal(error.data[0].expected_size, expectedSize);
    });
}

test("close without peer end is a failure rather than clean EOF", async () => {
    const { socket, connection } = createConnection();
    const reading = connection.read();
    socket.emit("close");
    assert.equal(errorOf(await reading).type, "protocol_error");
});

test("oversized reads and writes carry the observed size and protocol limit", async () => {
    const { socket, connection } = createConnection();
    const header = Buffer.alloc(4);
    header.writeUInt32LE(maxFrameSize + 1);
    socket.emit("data", header);
    const readError = errorOf(await connection.read());
    const writeError = errorOf(await connection.write(Buffer.alloc(maxFrameSize + 1)));
    for (const error of [readError, writeError]) {
        assert.deepEqual(error.data, [{ size: maxFrameSize + 1, max_frame_size: maxFrameSize }]);
        assert.equal(error.type, "protocol_error");
    }
    assert.equal(socket.writes.length, 0);
});

test("socket error events wake a pending read and retain native details", async () => {
    const { socket, connection } = createConnection();
    const original = nativeError();
    const reading = connection.read();
    socket.emit("error", original);
    const error = errorOf(await reading);
    assert.equal(error.message, original.message);
    assert.ok(error.data.some((entry) => entry?.code === original.code && entry.stack === original.stack));
    assert.equal(errorOf(await connection.write(Buffer.from("data"))), error);
});

test("client send, receive and messages forward the same lower error", async () => {
    const { socket, connection } = createConnection();
    const lower = { source: "sandbox", operation: "grant", type: "system_error", message: "original", data: [{ code: 5 }], causes: [] };
    const client = new IpcConnection(connection);
    socket.emit("error", lower);
    assert.equal(errorOf(await client.send({ payload: true })), lower);
    assert.equal(errorOf(await client.receive()), lower);
    const stream = client.messages();
    assert.equal(errorOf((await stream.next()).value), lower);
    assert.deepEqual(await stream.next(), { value: undefined, done: true });
});

test("messages emits successful Results and terminates normally at clean EOF", async () => {
    const { socket, connection } = createConnection();
    const client = new IpcConnection(connection);
    socket.emit("data", frame(valueOf(encode({ message: "hello" }))));
    socket.emit("end");
    const messages = [];
    for await (const result of client.messages()) {
        messages.push(valueOf(result));
    }
    assert.deepEqual(messages, [{ message: "hello" }]);
});

test("receive and messages expose malformed CBOR through Result rather than rejecting", async () => {
    const { socket, connection } = createConnection();
    const client = new IpcConnection(connection);
    socket.emit("data", frame(Buffer.from([0x1a, 0])));
    const stream = client.messages();
    const error = errorOf((await stream.next()).value);
    assert.equal(error.operation, "decode");
    assert.equal(error.type, "protocol_error");
    assert.equal((await stream.next()).done, true);
});

test("concurrent writes stay ordered and return successful Results", async () => {
    const { socket, connection } = createConnection();
    const results = await Promise.all([
        connection.write(Buffer.from("one")),
        connection.write(Buffer.from("two")),
        connection.write(Buffer.alloc(0)),
    ]);
    results.forEach(valueOf);
    assert.deepEqual(Buffer.concat(socket.writes), Buffer.concat([
        frame(Buffer.from("one")), frame(Buffer.from("two")), frame(Buffer.alloc(0)),
    ]));
});

for (const mode of ["writeError", "callbackError", "uncorkError"]) {
    test(`${mode} retains the original failure and does not reject the write queue`, async () => {
        const { socket, connection } = createConnection();
        const original = nativeError(mode);
        socket[mode] = original;
        const result = await connection.write(Buffer.from("payload"));
        const error = errorOf(result);
        assert.equal(error.message, original.message);
        assert.ok(error.data.some((entry) => entry?.code === original.code && entry.path === original.path));
        assert.equal(errorOf(await connection.write(Buffer.from("next"))), error);
    });
}

test("write and uncork failures remain distinct structured causes", async () => {
    const { socket, connection } = createConnection();
    socket.writeError = nativeError("write failed");
    socket.uncorkError = nativeError("uncork failed");
    const error = errorOf(await connection.write(Buffer.from("payload")));
    assert.equal(error.type, "dependency_error");
    assert.deepEqual(error.causes.map((cause) => cause.message), ["write failed", "uncork failed"]);
    assert.equal(errorOf(await connection.write(Buffer.from("next"))), error);
    assert.equal(errorOf(await connection.read()), error);
});

test("setup failure destroys the socket and keeps independent cleanup failure", () => {
    const socket = new TestSocket();
    socket.noDelayError = nativeError("setup failed");
    socket.destroyError = nativeError("cleanup failed");
    const error = errorOf(Connection.create(socket));
    assert.equal(socket.destroyCalls, 1);
    assert.equal(error.type, "dependency_error");
    assert.deepEqual(error.causes.map((cause) => cause.message), ["setup failed", "cleanup failed"]);
});

for (const [method, field, operation] of [
    ["closeTransport", "endError", "close"],
    ["destroyTransport", "destroyError", "destroy"],
]) {
    test(`${method} returns synchronous native failures through Result`, () => {
        const { socket, connection } = createConnection();
        const client = new IpcConnection(connection);
        socket[field] = nativeError();
        const error = errorOf(client[method]());
        assert.equal(error.operation, operation);
        assert.equal(error.message, socket[field].message);
        assert.ok(error.data.some((entry) => entry?.errno === socket[field].errno));
    });
}
