import { failure, success } from "../result.js";
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
            return failure({
                ...context, operation, direction: "input",
                cause: api.validateInput.errors,
            });
        }

        operation = "invoke";
        const output = await loaded.plugin.invoke({ api: api.descriptor.id, input });
        operation = "output_validate";
        if (!api.validateOutput(output)) {
            return failure({
                ...context, operation, direction: "output",
                cause: api.validateOutput.errors,
            });
        }
        return success(output);
    } catch (cause) {
        return failure({ ...context, operation, cause });
    }
}

