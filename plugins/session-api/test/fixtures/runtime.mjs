import { connect } from "@hh/ipc-client";
import { failure, normalizeError, success } from "../../dist/error.js";

const providerError = {
    source: "provider", operation: "request", type: "http_error",
    message: "Original HTTP 429 failure", data: [{
        status_code: 429, status_line: "HTTP/1.1 429 Too Many Requests",
        body: { original: ["provider", null, true] }, stack: "original provider stack", path: "provider-path",
    }], causes: [],
};
const primary = {
    source: "sessions", operation: "run_request", type: "dependency_error",
    message: "Provider request failed", data: [null, true, 12, "text", [1, { arbitrary: "JSON" }]],
    causes: [providerError],
};
const secondary = {
    source: "sandbox", operation: "release", type: "system_error",
    message: "Original cleanup failure", data: [{ code: 5, category: "system", api: "fixture.cleanup", path: "cleanup-path" }],
    causes: [],
};

async function run() {
    const connected = await connect(process.argv[2]);
    if (connected.error !== null) return failure(connected.error);
    const connection = connected.value;
    let sequence = 1n;
    const publish = (source, level, type, data) => connection.send([
        sequence++, BigInt(Date.now()), source, level, type, [["runtime", "fixture"]], data,
    ]);
    let outcome = await publish("session_runtime", 2, "ready", { protocol_version: 1 });
    if (outcome.error !== null) {
        connection.destroyTransport();
        return outcome;
    }
    try {
        for await (const received of connection.messages()) {
            if (received.error !== null) {
                outcome = received;
                break;
            }
            const [commandId, kind] = received.value;
            if (kind === 3) {
                outcome = await publish("session_runtime", 4, "command_failed", { command_id: commandId, error: primary });
                if (outcome.error !== null) break;
                outcome = await publish("sessions", 4, "secondary_error", { error: secondary });
            } else {
                outcome = await publish("session_runtime", 2, "command_finished", {
                    command_id: commandId, state: "ready", result: { kind },
                });
            }
            if (outcome.error !== null) break;
        }
    } catch (error) {
        outcome = failure(normalizeError(error, "fixture_runtime", "protocol_error"));
    }
    const cleanup = connection.destroyTransport();
    if (cleanup.error !== null) return cleanup;
    return outcome.error === null ? success(undefined) : outcome;
}

const outcome = await run();
if (outcome.error !== null) {
    console.error(outcome.error);
    process.exitCode = 1;
}
