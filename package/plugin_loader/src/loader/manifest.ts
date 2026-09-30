import manifestSchema from "../plugin.schema.json" with { type: "json" };
import type { PluginManifest } from "../plugin.js";
import { captureError, makeError } from "../error.js";
import { failure, success } from "../result.js";
import type { PluginResult } from "../result.js";
import { createValidator } from "./schema.js";

function hasRequiredGetErrorContract(manifest: PluginManifest): boolean {
    const api = manifest.apis.find((candidate) => candidate.name === "get_error");
    if (api === undefined) {
        return false;
    }

    const input = api.input;
    const output = api.output;
    if (
        typeof input !== "object"
        || input === null
        || Array.isArray(input)
        || input.type !== "object"
        || input.additionalProperties !== false
        || !Array.isArray(input.required)
        || input.required.length !== 0
        || typeof input.properties !== "object"
        || input.properties === null
        || Array.isArray(input.properties)
        || Object.keys(input.properties).length !== 0
    ) {
        return false;
    }

    return (
        typeof output === "object"
        && output !== null
        && !Array.isArray(output)
        && output.type === "array"
        && (output.items === undefined || output.items === true
            || (typeof output.items === "object" && output.items !== null && !Array.isArray(output.items)))
    );
}

export function parseManifest(
    value: unknown, context: Record<string, unknown> = {},
): PluginResult<PluginManifest> {
    try {
        const validate = createValidator().compile<PluginManifest>(manifestSchema);
        if (!validate(value)) {
            return failure(makeError("manifest_validate", "validation_error", "Manifest validation failed",
                [{ ...context, schema_errors: structuredClone(validate.errors) }]));
        }

        const ids = new Set<number>();
        const names = new Set<string>();
        for (const [index, api] of value.apis.entries()) {
            const field = "apis[" + index + "]";
            if (!Number.isSafeInteger(api.id)) {
                return failure(makeError("manifest_validate", "validation_error", "API ID is not a safe integer",
                    [{ ...context, pluginId: value.id, apiId: api.id,
                        code: "unsafe_api_id", field: field + ".id", value: api.id }]));
            }
            if (ids.has(api.id) || names.has(api.name)) {
                return failure(makeError("manifest_validate", "validation_error", "Duplicate API identifier",
                    [{ ...context, pluginId: value.id, apiId: api.id,
                        code: ids.has(api.id) ? "duplicate_api_id" : "duplicate_api_name",
                        field: field + (ids.has(api.id) ? ".id" : ".name"),
                        value: ids.has(api.id) ? api.id : api.name,
                    }]));
            }
            ids.add(api.id);
            names.add(api.name);
        }
        if (!value.apis.some((api) => api.name === "get_error")) {
            return failure(makeError("manifest_validate", "protocol_error", "Required API is missing",
                [{ ...context, pluginId: value.id, code: "missing_required_api", value: "get_error" }]));
        }
        if (!hasRequiredGetErrorContract(value)) {
            return failure(makeError("manifest_validate", "protocol_error", "Required API contract is invalid",
                [{ ...context, pluginId: value.id, code: "invalid_required_api_contract", value: "get_error" }]));
        }
        return success(value);
    } catch (cause) {
        return failure(captureError("manifest_validate", cause, context));
    }
}
