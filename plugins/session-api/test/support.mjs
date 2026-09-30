import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { inspect } from "node:util";
import { RuntimeHost } from "../dist/runtime/host.js";
import { failure, success } from "../dist/error.js";

export function valueOf(result) {
    assert.equal(result.error, null, inspect(result.error));
    return result.value;
}
export function errorOf(result) {
    assert.equal(result.value, null);
    assert.ok(result.error);
    assert.deepEqual(Object.keys(result.error).sort(), ["causes", "data", "message", "operation", "source", "type"]);
    assert.ok(Array.isArray(result.error.data));
    assert.ok(Array.isArray(result.error.causes));
    return result.error;
}
export function hhError(message = "original failure", source = "provider") {
    return {
        source, operation: "request", type: "http_error", message,
        data: [{ status_code: 429, body: { original: [1, null, true] }, path: "original-path", code: 5 }],
        causes: [],
    };
}
export function nativeError(message = "original native failure") {
    return Object.assign(new Error(message), {
        code: "ENOENT", errno: -2, syscall: "spawn", path: "original-executable",
        spawnargs: ["original-argument"], detail: { original: [1, null] },
    });
}
export function wire(type = "ready", data = { protocol_version: 1 }, source = "session_runtime", level = 2) {
    return [1n, 2n, source, level, type, [["runtime", "example"]], data];
}

export class TestChild extends EventEmitter {
    pid = 1234;
    exitCode = null;
    signalCode = null;
    killError;
    killAccepted = true;
    killCalls = 0;
    exited = false;
    kill() {
        this.killCalls++;
        if (this.killError !== undefined) throw this.killError;
        if (this.killAccepted) this.exit(null, "SIGTERM");
        return this.killAccepted;
    }
    exit(code = 0, signal = null) {
        if (this.exited) return;
        this.exited = true;
        this.exitCode = code;
        this.signalCode = signal;
        this.emit("exit", code, signal);
        this.emit("close", code, signal);
    }
}

export class TestConnection {
    incoming = [];
    waiters = [];
    sent = [];
    sendError;
    closeError;
    destroyError;
    closed = false;
    closeCalls = 0;
    destroyCalls = 0;
    onClose = () => {};
    onSend = () => {};
    receive() {
        if (this.incoming.length > 0) return Promise.resolve(this.incoming.shift());
        if (this.closed) return Promise.resolve(success(null));
        return new Promise((resolve) => this.waiters.push(resolve));
    }
    push(value) { this.pushResult(success(value)); }
    pushResult(result) {
        const waiter = this.waiters.shift();
        if (waiter) waiter(result);
        else this.incoming.push(result);
    }
    async send(message) {
        if (this.sendError !== undefined) return failure(this.sendError);
        this.sent.push(message);
        this.onSend(message);
        return success(undefined);
    }
    closeTransport() {
        this.closeCalls++;
        if (this.closeError !== undefined) return failure(this.closeError);
        this.eof();
        this.onClose();
        return success(undefined);
    }
    destroyTransport() {
        this.destroyCalls++;
        if (this.destroyError !== undefined) return failure(this.destroyError);
        this.eof();
        return success(undefined);
    }
    eof() {
        if (this.closed) return;
        this.closed = true;
        this.push(null);
    }
}

export class TestServer {
    connection;
    accepted = false;
    acceptError;
    deferAccept = false;
    closeError;
    closeCalls = 0;
    constructor(connection) { this.connection = connection; }
    async accept() {
        if (this.acceptError !== undefined) return failure(this.acceptError);
        if (this.deferAccept) return new Promise(() => {});
        this.accepted = true;
        return success(this.connection);
    }
    async close() {
        this.closeCalls++;
        if (!this.accepted) this.connection.destroyTransport();
        return this.closeError === undefined ? success(undefined) : failure(this.closeError);
    }
}

export function fakeHost(options = {}) {
    const child = new TestChild();
    const connection = new TestConnection();
    const server = new TestServer(connection);
    connection.onClose = () => child.exit();
    Object.assign(server, options.server);
    const host = new RuntimeHost({
        async listen(name) {
            server.name = name;
            return options.listenError ? failure(options.listenError) : success(server);
        },
        spawn(executable, name) {
            child.executable = executable;
            child.pipeName = name;
            options.onSpawn?.(child);
            return options.spawnError ? failure(options.spawnError) : success(child);
        },
    });
    return { host, child, connection, server };
}
export async function opened(options = {}) {
    const fixture = fakeHost(options);
    fixture.id = valueOf(await fixture.host.openRuntime("test-executable")).runtime_id;
    return fixture;
}
