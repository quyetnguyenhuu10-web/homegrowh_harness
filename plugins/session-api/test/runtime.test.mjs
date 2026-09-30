import assert from "node:assert/strict";
import test from "node:test";
import { errorOf, fakeHost, hhError, nativeError, opened, valueOf, wire } from "./support.mjs";

test("lookup and closed stream failures are structured for every runtime operation", async () => {
    const { host } = fakeHost();
    for (const call of [
        () => host.sendRuntime(1, null), () => host.receiveRuntime(1),
        () => host.streamRuntimeEvent(1), () => host.streamRuntimeError(1),
        () => host.executeRuntimeCommand(1, 1, [1n, 2]), () => host.closeRuntime(1),
    ]) {
        assert.equal(errorOf(await call()).type, "state_error");
    }
    assert.equal(errorOf(await host.closeRuntime(-1)).type, "validation_error");
});

test("startup forwards the original IPC error unchanged", async () => {
    const original = hhError("listen failed", "ipc_client");
    const { host } = fakeHost({ listenError: original });
    assert.equal(errorOf(await host.openRuntime("executable")), original);
});

test("startup retains spawn and listener cleanup failures as independent causes", async () => {
    const original = hhError("spawn failure", "process");
    const cleanup = hhError("listener cleanup", "ipc_client");
    const { host, server } = fakeHost({ spawnError: original, server: { closeError: cleanup } });
    const error = errorOf(await host.openRuntime("executable"));
    assert.deepEqual(error.causes, [original, cleanup]);
    assert.equal(server.closeCalls, 1);
});

test("successful startup cleanup forwards the IPC failure without counting intentional process termination as a new error", async () => {
    const original = hhError("accept failed", "ipc_client");
    const { host, child, server } = fakeHost({ server: { acceptError: original } });
    assert.equal(errorOf(await host.openRuntime("executable")), original);
    assert.equal(child.killCalls, 1);
    assert.equal(child.exited, true);
    assert.equal(server.closeCalls, 1);
});

test("asynchronous native spawn errors retain errno, path, spawnargs and stack", async () => {
    const original = nativeError();
    const { host, child, server } = fakeHost({
        server: { deferAccept: true },
        onSpawn(process) {
            process.pid = undefined;
            queueMicrotask(() => { process.emit("error", original); process.emit("close", -1, null); });
        },
    });
    const error = errorOf(await host.openRuntime("executable"));
    assert.equal(error.message, original.message);
    assert.ok(error.data.some(data => data.code === original.code && data.path === original.path && data.stack === original.stack));
    assert.equal(server.closeCalls, 1);
    assert.equal(child.listenerCount("error"), 0);
});

test("exit before connection reports the observed exit status in data", async () => {
    const { host } = fakeHost({ server: { deferAccept: true }, onSpawn(child) { queueMicrotask(() => child.exit(17)); } });
    const error = errorOf(await host.openRuntime("executable"));
    assert.equal(error.type, "state_error");
    assert.equal(error.data[0].exit_code, 17);
    assert.equal(Object.hasOwn(error, "code"), false);
});

test("command completion is correlated and duplicate pending ids cannot overwrite waiters", async () => {
    const { host, id, connection } = await opened();
    const pending = host.executeRuntimeCommand(id, 10, [10n, 2]);
    assert.equal(errorOf(await host.executeRuntimeCommand(id, 10n, [10n, 2])).type, "state_error");
    assert.deepEqual(connection.sent, [[10n, 2]]);
    connection.push(wire("command_finished", { command_id: 10n, state: "ready", result: { original: true } }));
    assert.deepEqual(valueOf(await pending), { command_id: 10n, state: "ready", result: { original: true } });
    valueOf(await host.closeRuntime(id));
});

test("command failure, error stream and event stream preserve the same lower error and event envelope", async () => {
    const { host, id, connection } = await opened();
    const original = hhError();
    const pending = host.executeRuntimeCommand(id, 10n, [10n, 3]);
    const errorStream = host.streamRuntimeError(id);
    const eventStream = host.streamRuntimeEvent(id);
    const raw = wire("command_failed", { command_id: 10n, error: original }, "session_runtime", 4);
    connection.push(raw);
    assert.equal(errorOf(await pending), original);
    assert.equal(valueOf(await errorStream), original);
    assert.equal(valueOf(await eventStream), raw);
    valueOf(await host.closeRuntime(id));
});

test("fatal runtime event fails all commands while retaining the event for both streams", async () => {
    const { host, id, connection } = await opened();
    const first = host.executeRuntimeCommand(id, 1n, [1n, 2]);
    const second = host.executeRuntimeCommand(id, 2n, [2n, 2]);
    const original = hhError("runtime dependency failed");
    const raw = wire("runtime_failed", { error: original, exit_code: 2 }, "session_runtime", 4);
    connection.push(raw);
    assert.equal(errorOf(await first), original);
    assert.equal(errorOf(await second), original);
    assert.equal(errorOf(await host.executeRuntimeCommand(id, 3n, [3n, 2])), original);
    assert.equal(valueOf(await host.streamRuntimeEvent(id)), raw);
    assert.equal(valueOf(await host.streamRuntimeError(id)), original);
    assert.equal(errorOf(await host.closeRuntime(id)), original);
});

test("malformed events fail pending consumers and stop the reader without rejection", async () => {
    const { host, id, connection } = await opened();
    const command = host.executeRuntimeCommand(id, 1n, [1n, 2]);
    const event = host.streamRuntimeEvent(id);
    const error = host.streamRuntimeError(id);
    const raw = ["malformed"];
    connection.push(raw);
    const original = errorOf(await command);
    assert.equal(original.operation, "parse_event");
    assert.equal(original.data[0].raw, raw);
    assert.equal(errorOf(await event), original);
    assert.equal(errorOf(await error), original);
    assert.equal(connection.destroyCalls, 1);
    assert.equal(errorOf(await host.closeRuntime(id)), original);
});

test("multiple fatal events retain every original failure for later commands and shutdown", async () => {
    const { host, id, connection } = await opened();
    const first = hhError("first fatal failure");
    const second = hhError("second fatal failure", "sandbox");
    connection.push(wire("runtime_failed", { error: first }, "session_runtime", 4));
    connection.push(wire("runtime_failed", { error: second }, "session_runtime", 4));
    assert.equal(valueOf(await host.streamRuntimeError(id)), first);
    assert.equal(valueOf(await host.streamRuntimeError(id)), second);
    const error = errorOf(await host.executeRuntimeCommand(id, 1n, [1n, 2]));
    assert.deepEqual(error.causes, [first, second]);
    assert.equal(errorOf(await host.closeRuntime(id)), error);
});

test("EOF with pending commands records their exact ids in a state error", async () => {
    const { host, id, connection } = await opened();
    const pending = host.executeRuntimeCommand(id, 99n, [99n, 2]);
    connection.eof();
    const error = errorOf(await pending);
    assert.equal(error.operation, "read_events");
    assert.deepEqual(error.data[0].command_ids, ["99"]);
    assert.equal(errorOf(await host.closeRuntime(id)), error);
});

test("send failure removes its waiter so the same id can be retried", async () => {
    const { host, id, connection } = await opened();
    const original = hhError("write failed", "ipc_client");
    connection.sendError = original;
    assert.equal(errorOf(await host.executeRuntimeCommand(id, 1n, [1n, 2])), original);
    connection.sendError = undefined;
    const retried = host.executeRuntimeCommand(id, 1n, [1n, 2]);
    connection.push(wire("command_finished", { command_id: 1n, state: "ready", result: null }));
    assert.equal(valueOf(await retried).state, "ready");
    valueOf(await host.closeRuntime(id));
});

test("late native process errors fail consumers with the original properties", async () => {
    const { host, id, child } = await opened();
    const original = nativeError("late process failure");
    const pending = host.streamRuntimeEvent(id);
    child.emit("error", original);
    const error = errorOf(await pending);
    assert.equal(error.operation, "runtime_process");
    assert.equal(error.message, original.message);
    assert.ok(error.data.some(data => data.code === original.code && data.stack === original.stack));
    assert.equal(errorOf(await host.sendRuntime(id, null)), error);
    assert.equal(errorOf(await host.closeRuntime(id)), error);
});

test("shutdown preserves graceful-close, kill and listener failures and permits cleanup retry", async () => {
    const { host, id, child, connection, server } = await opened();
    const closeError = hhError("graceful close failed", "ipc_client");
    const killError = nativeError("kill failed");
    const serverError = hhError("listener close failed", "ipc_client");
    connection.closeError = closeError;
    child.killError = killError;
    // The listener close began when opening, so supply its failure through that result.
    const other = await opened({ server: { closeError: serverError } });
    other.connection.closeError = closeError;
    other.child.killError = killError;
    const error = errorOf(await other.host.closeRuntime(other.id));
    assert.equal(error.type, "dependency_error");
    assert.deepEqual(error.causes.map(cause => cause.message), [closeError.message, killError.message, serverError.message]);
    other.connection.closeError = undefined;
    other.child.killError = undefined;
    await other.host.closeRuntime(other.id);
    connection.closeError = undefined;
    child.killError = undefined;
    valueOf(await host.closeRuntime(id));
    assert.equal(server.closeCalls, 1);
});

test("failed destruction and unaccepted termination return without hanging on unreleased resources", { timeout: 1000 }, async () => {
    const { host, id, child, connection } = await opened();
    connection.closeError = hhError("close failed", "ipc_client");
    connection.destroyError = hhError("destroy failed", "ipc_client");
    child.killAccepted = false;
    const error = errorOf(await host.closeRuntime(id));
    assert.equal(error.type, "dependency_error");
    assert.equal(error.causes.length, 3);
    connection.closeError = undefined;
    connection.destroyError = undefined;
    child.killAccepted = true;
    await host.closeRuntime(id);
});

test("normal shutdown returns the observed process result and closes both streams", async () => {
    const { host, id } = await opened();
    const pendingEvent = host.streamRuntimeEvent(id);
    const pendingError = host.streamRuntimeError(id);
    assert.deepEqual(valueOf(await host.closeRuntime(id)), { code: 0, signal: null });
    assert.equal(valueOf(await pendingEvent), null);
    assert.equal(valueOf(await pendingError), null);
    assert.equal(errorOf(await host.sendRuntime(id, null)).type, "state_error");
});
