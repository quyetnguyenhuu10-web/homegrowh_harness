import assert from "node:assert/strict";
import { EventEmitter } from "node:events";

export function valueOf(result) {
    assert.equal(result.error, null, JSON.stringify(result.error));
    return result.value;
}

export function errorOf(result) {
    assert.equal(result.value, null);
    assert.ok(result.error);
    assert.deepEqual(Object.keys(result.error).sort(), [
        "causes", "data", "message", "operation", "source", "type",
    ]);
    assert.ok(Array.isArray(result.error.data));
    assert.ok(Array.isArray(result.error.causes));
    return result.error;
}

export function nativeError(message = "original native failure") {
    return Object.assign(new Error(message), {
        code: "EACCES",
        errno: -13,
        syscall: "test syscall",
        path: "original-path",
        detail: { nested: [1, null, false] },
    });
}

export function frame(payload) {
    const header = Buffer.alloc(4);
    header.writeUInt32LE(payload.length);
    return Buffer.concat([header, payload]);
}

export class TestSocket extends EventEmitter {
    destroyed = false;
    destroyCalls = 0;
    writes = [];
    noDelayError;
    writeError;
    callbackError;
    uncorkError;
    endError;
    destroyError;

    setNoDelay() {
        if (this.noDelayError !== undefined) {
            throw this.noDelayError;
        }
        return this;
    }

    cork() {}

    uncork() {
        if (this.uncorkError !== undefined) {
            throw this.uncorkError;
        }
    }

    write(data, callback) {
        if (this.writeError !== undefined) {
            throw this.writeError;
        }
        this.writes.push(Buffer.from(data));
        queueMicrotask(() => callback?.(this.callbackError));
        return true;
    }

    end() {
        if (this.endError !== undefined) {
            throw this.endError;
        }
        return this;
    }

    destroy() {
        this.destroyCalls++;
        if (this.destroyError !== undefined) {
            throw this.destroyError;
        }
        this.destroyed = true;
        queueMicrotask(() => this.emit("close"));
        return this;
    }
}

export class TestServer extends EventEmitter {
    closeError;
    throwOnClose = false;
    closeCalls = 0;

    close(callback) {
        this.closeCalls++;
        if (this.throwOnClose) {
            throw this.closeError;
        }
        queueMicrotask(() => {
            if (this.closeError === undefined) {
                this.emit("close");
            }
            callback(this.closeError);
        });
        return this;
    }
}
