import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { test } from "node:test";
import { parseEvent, eventError, isErrorEvent } from "../../../plugins/session-api/dist/events.js";

for (const producer of ["sessions_error_boundary_test", "sessions_turn_failure_test"]) {
    test(`${producer} events satisfy session-api`, context => {
        const suffix = process.platform === "win32" ? ".exe" : "";
        const directory = process.env.SESSIONS_TEST_EXECUTABLE_DIR ?? fileURLToPath(new URL(
            `../../../build/package/sessions/${process.platform === "win32" ? "Release/" : ""}`, import.meta.url));
        const executable = resolve(directory, `${producer}${suffix}`);
        const output = execFileSync(executable, ["--wire"], { encoding: "utf8", timeout: 30_000 });
        const events = output.trim().split(/\r?\n/).map(line => JSON.parse(line));
        let errors = 0;
        for (const wire of events) {
            const result = parseEvent(wire);
            assert.equal(result.error, null, JSON.stringify(result.error));
            assert.deepEqual(result.value.data, wire[6]);
            if (isErrorEvent(result.value)) {
                errors++;
                assert.strictEqual(eventError(result.value), wire[6].error);
            }
        }
        assert.ok(errors > 0, "Producer did not exercise any error events");
        context.diagnostic(`${events.length} native events parsed, including ${errors} error events`);
    });
}
