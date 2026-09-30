import assert from "node:assert/strict";
import { is_hh_error } from "@hh/plugin-loader";

export function assertHHError(error) {
    assert.ok(is_hh_error(error));
    assert.deepEqual(Object.keys(error).sort(),
        ["source", "operation", "type", "message", "data", "causes"].sort());
    assert.deepEqual(JSON.parse(JSON.stringify(error)), error);
    for (const cause of error.causes) assertHHError(cause);
    return error;
}

export function field(error, name) {
    assertHHError(error);
    return error.data.find((item) => typeof item === "object" && item !== null
        && Object.hasOwn(item, name))?.[name];
}
