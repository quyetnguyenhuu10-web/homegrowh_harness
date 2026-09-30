import assert from "node:assert/strict";
import test from "node:test";
import * as net from "node:net";
import { randomUUID } from "node:crypto";
import { connect, listen, Server } from "../dist/ipc.js";
import { IpcServer } from "../dist/index.js";
import { errorOf, nativeError, TestServer, TestSocket, valueOf } from "./support.mjs";

test("accept wakes with the original server error and the client forwards it", async () => {
    const nativeServer = new TestServer();
    const server = new Server(nativeServer);
    const client = new IpcServer(server);
    const lower = { source: "plugin_loader", operation: "invoke", type: "dependency_error", message: "original", data: [{ plugin_id: "example" }], causes: [] };
    const pending = client.accept();
    nativeServer.emit("error", lower);
    assert.equal(errorOf(await pending), lower);
    assert.equal(errorOf(await client.accept()), lower);
    valueOf(await client.close());
});

test("native server events preserve original codes and structured properties", async () => {
    const nativeServer = new TestServer();
    const server = new Server(nativeServer);
    const original = nativeError();
    const accepting = server.accept();
    nativeServer.emit("error", original);
    const error = errorOf(await accepting);
    assert.equal(error.operation, "accept");
    assert.equal(error.message, original.message);
    assert.ok(error.data.some((entry) => entry?.syscall === original.syscall && entry.stack === original.stack));
    valueOf(await server.close());
});

test("accepted socket setup errors are delivered to accept without escaping the event handler", async () => {
    const nativeServer = new TestServer();
    const server = new Server(nativeServer);
    const socket = new TestSocket();
    socket.noDelayError = nativeError("configuration failed");
    const accepting = server.accept();
    assert.doesNotThrow(() => nativeServer.emit("connection", socket));
    const error = errorOf(await accepting);
    assert.equal(error.operation, "configure_connection");
    assert.equal(error.message, socket.noDelayError.message);
    assert.equal(socket.destroyed, true);
    valueOf(await server.close());
});

test("server shutdown destroys unaccepted sockets and finishes all accept waiters", async () => {
    const nativeServer = new TestServer();
    const server = new Server(nativeServer);
    const socket = new TestSocket();
    nativeServer.emit("connection", socket);
    valueOf(await server.close());
    assert.equal(socket.destroyed, true);
    assert.equal(errorOf(await server.accept()).type, "state_error");

    const waitingServer = new Server(new TestServer());
    const waiting = [waitingServer.accept(), waitingServer.accept()];
    valueOf(await waitingServer.close());
    for (const pending of waiting) {
        assert.equal(errorOf(await pending).type, "state_error");
    }
});

test("multiple pending socket cleanup failures remain separate causes", async () => {
    const nativeServer = new TestServer();
    const server = new Server(nativeServer);
    for (const message of ["first cleanup", "second cleanup"]) {
        const socket = new TestSocket();
        socket.destroyError = nativeError(message);
        nativeServer.emit("connection", socket);
    }
    nativeServer.closeError = nativeError("listener cleanup");
    const error = errorOf(await server.close());
    assert.equal(error.operation, "close");
    assert.equal(error.type, "dependency_error");
    assert.deepEqual(error.causes.map((cause) => cause.message), [
        "first cleanup", "second cleanup", "listener cleanup",
    ]);
});

for (const throwOnClose of [false, true]) {
    test(`server close preserves native ${throwOnClose ? "thrown" : "callback"} errors and is shared by concurrent calls`, async () => {
        const nativeServer = new TestServer();
        const server = new Server(nativeServer);
        nativeServer.closeError = nativeError();
        nativeServer.throwOnClose = throwOnClose;
        const first = server.close();
        const second = server.close();
        assert.equal(first, second);
        const error = errorOf(await first);
        assert.equal(error.operation, "close");
        assert.equal(error.message, nativeServer.closeError.message);
        assert.ok(error.data.some((entry) => entry?.errno === nativeServer.closeError.errno));
        assert.equal(nativeServer.closeCalls, 1);
    });
}

for (const name of ["", "bad/name", "bad\\name", "bad\0name", null, 42]) {
    test(`invalid endpoint ${JSON.stringify(name)} returns validation errors`, async () => {
        for (const [operation, invoke] of [["connect", connect], ["listen", listen]]) {
            const error = errorOf(await invoke(name));
            assert.equal(error.operation, operation);
            assert.equal(error.type, "validation_error");
            assert.deepEqual(error.data, [{ name }]);
        }
    });
}

test("synchronous socket connect failures become Results and remove temporary listeners", async (t) => {
    const original = nativeError("native connect failed");
    let socket;
    t.mock.method(net.Socket.prototype, "connect", function () {
        socket = this;
        throw original;
    });
    const error = errorOf(await connect(`ipc-client-test-${randomUUID()}`));
    assert.equal(error.operation, "connect");
    assert.equal(error.message, original.message);
    assert.ok(error.data.some((entry) => entry?.code === original.code && entry.path === original.path));
    assert.ok(error.data.some((entry) => entry?.api === "net.Socket.connect" && typeof entry.path === "string"));
    assert.equal(socket.destroyed, true);
    assert.equal(socket.listenerCount("connect"), 0);
    assert.equal(socket.listenerCount("error"), 1);
});

test("synchronous listener failures become Results and remove temporary listeners", async (t) => {
    const original = nativeError("native listen failed");
    let nativeServer;
    t.mock.method(net.Server.prototype, "listen", function () {
        nativeServer = this;
        throw original;
    });
    const error = errorOf(await listen(`ipc-client-test-${randomUUID()}`));
    assert.equal(error.operation, "listen");
    assert.equal(error.message, original.message);
    assert.ok(error.data.some((entry) => entry?.code === original.code && entry.stack === original.stack));
    assert.equal(nativeServer.listening, false);
    assert.equal(nativeServer.listenerCount("listening"), 0);
    assert.equal(nativeServer.listenerCount("error"), 1);
});
