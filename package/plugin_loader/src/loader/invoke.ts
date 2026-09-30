import { failure, success } from "../result.js";
import { captureError, is_hh_error, makeError, normalize_error } from "../error.js";
import type { PluginLoaderOperation, PluginResult } from "../result.js";
import type { LoadedApi, LoadedPlugin } from "./types.js";

export async function invokeLoadedApi(
    loaded: LoadedPlugin,
    api: LoadedApi,
    input: unknown,
): Promise<PluginResult<unknown>> {
    let operation: PluginLoaderOperation = "input_validate";
    const context = { pluginId: loaded.manifest.id, apiId: api.descriptor.id };
    try {
        if (!api.validateInput(input)) {
            return failure(makeError(operation, "validation_error", "Plugin input validation failed",
                [{ ...context, direction: "input", schema_errors: structuredClone(api.validateInput.errors) }]));
        }

        operation = "invoke";
        let output = await loaded.plugin.invoke({ api: api.descriptor.id, input });
        operation = "output_validate";
        if (api.descriptor.name === "get_error" && Array.isArray(output) && !output.every(is_hh_error)) {
            output = output.map((error) => normalize_error(error, {
                source: loaded.manifest.id, operation: "invoke", data: [context],
            }));
        }
        if (!api.validateOutput(output)) {
            return failure(makeError(operation, "validation_error", "Plugin output validation failed",
                [{ ...context, direction: "output", schema_errors: structuredClone(api.validateOutput.errors) }]));
        }
        return success(output);
    } catch (cause) {
        return failure(captureError(operation, cause, context));
    }
}

