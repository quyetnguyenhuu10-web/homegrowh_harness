import manifestSchema from "../plugin.schema.json" with { type: "json" };
import type { PluginManifest } from "../plugin.js";
import { failure, success } from "../result.js";
import type { PluginResult } from "../result.js";
import { createValidator } from "./schema.js";

export function parseManifest(value: unknown): PluginResult<PluginManifest> {
    try {
        const validate = createValidator().compile<PluginManifest>(manifestSchema);
        if (!validate(value)) {
            return failure({ operation: "manifest_validate", cause: validate.errors });
        }

        const ids = new Set<number>();
        const names = new Set<string>();
        for (const [index, api] of value.apis.entries()) {
            const field = "apis[" + index + "]";
            if (!Number.isSafeInteger(api.id)) {
                return failure({
                    operation: "manifest_validate",
                    cause: { code: "unsafe_api_id", field: field + ".id", value: api.id },
                    pluginId: value.id, apiId: api.id,
                });
            }
            if (ids.has(api.id) || names.has(api.name)) {
                return failure({
                    operation: "manifest_validate",
                    cause: {
                        code: ids.has(api.id) ? "duplicate_api_id" : "duplicate_api_name",
                        field: field + (ids.has(api.id) ? ".id" : ".name"),
                        value: ids.has(api.id) ? api.id : api.name,
                    },
                    pluginId: value.id, apiId: api.id,
                });
            }
            ids.add(api.id);
            names.add(api.name);
        }
        return success(value);
    } catch (cause) {
        return failure({ operation: "manifest_validate", cause });
    }
}

