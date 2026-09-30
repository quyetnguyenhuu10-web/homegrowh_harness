import type { Plugin, PluginRequest } from "./plugin.js";
import { failure, get_error } from "./result.js";
import type { PluginErrorInput, PluginLoaderError, PluginResult } from "./result.js";

const Api = { getError: 1 } as const;

function invoke(request: PluginRequest): PluginLoaderError | null | PluginResult<never> {
    try {
        switch (request.api) {
            case Api.getError:
                return get_error(request.input as PluginErrorInput);
            default:
                return failure({
                    operation: "invoke",
                    cause: { code: "unsupported_api", value: request.api },
                });
        }
    } catch (cause) {
        return failure({ operation: "invoke", cause });
    }
}

export const plugin: Plugin = { invoke };

