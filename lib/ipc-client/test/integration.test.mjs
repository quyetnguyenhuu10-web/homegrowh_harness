import assert from "node:assert/strict";
import test from "node:test";
import { randomUUID } from "node:crypto";
import { connect, listen } from "../dist/index.js";
import { errorOf, valueOf } from "./support.mjs";

test("real IPC transports a complete HHError envelope and ordered messages", { timeout: 10000 }, async (t) => {
    const name = `ipc-client-test-${randomUUID()}`;
    const server = valueOf(await listen(name));
    let sender;
    let receiver;
    t.after(async () => {
        if (sender !== undefined) valueOf(sender.destroyTransport());
        if (receiver !== undefined) valueOf(receiver.destroyTransport());
        valueOf(await server.close());
    });

    const accepting = server.accept();
    sender = valueOf(await connect(name));
    receiver = valueOf(await accepting);
    const error = {
        source: "sessions",
        operation: "run_request",
        type: "dependency_error",
        message: "Provider request failed",
        data: [null, true, 12, "text", [1, { arbitrary: "JSON" }]],
        causes: [{
            source: "provider",
            operation: "request",
            type: "http_error",
            message: "Original HTTP failure",
            data: [{ status_code: 429, body: { original: ["details"] }, stack: "original stack" }],
            causes: [],
        }],
    };
    const event = {
        package: "sessions",
        level: "error",
        type: "command_failed",
        references: [],
        data: { error },
    };

    valueOf(await sender.send(event));
    assert.deepEqual(valueOf(await receiver.receive()), event);
    (await Promise.all([sender.send({ sequence: 1 }), sender.send({ sequence: 2 })])).forEach(valueOf);
    assert.deepEqual(valueOf(await receiver.receive()), { sequence: 1 });
    assert.deepEqual(valueOf(await receiver.receive()), { sequence: 2 });
    valueOf(await receiver.send({ reply: true }));
    assert.deepEqual(valueOf(await sender.receive()), { reply: true });

    valueOf(sender.closeTransport());
    assert.equal(valueOf(await receiver.receive()), null);
});

test("missing real IPC endpoint retains the native code, syscall and requested path", { timeout: 10000 }, async () => {
    const name = `ipc-client-missing-${randomUUID()}`;
    const error = errorOf(await connect(name));
    assert.equal(error.source, "ipc_client");
    assert.equal(error.operation, "connect");
    assert.equal(error.type, "system_error");
    assert.ok(error.data.some((entry) => entry?.api === "net.Socket.connect" && entry.path.includes(name)));
    const native = error.data.find((entry) => typeof entry?.code === "string");
    assert.ok(native);
    assert.equal(native.syscall, "connect");
    assert.equal(typeof native.errno, "number");
    assert.equal(typeof native.stack, "string");
    assert.ok(error.message.length > 0);
    assert.deepEqual(JSON.parse(JSON.stringify(error)), error);
});
