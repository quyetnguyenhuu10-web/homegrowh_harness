import { PluginErrorQueue } from "./error_queue.js";
import type { Plugin, PluginRequest } from "./plugin.js";
import { makeError } from "./error.js";

const Api = { getError: 1 } as const;
const errors = new PluginErrorQueue();

async function invoke(request: PluginRequest): Promise<unknown> {
    if (request.api === Api.getError) {
        return errors.drain();
    }

    const error = errors.push(makeError("invoke", "protocol_error",
        "Unsupported plugin API", [{ code: "api_not_found", apiId: request.api }]));
    throw error;
}

export const plugin: Plugin = { invoke };
