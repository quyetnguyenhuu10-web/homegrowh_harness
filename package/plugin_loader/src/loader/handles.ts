import type { JsonSchema, PluginApiHandle, PluginHandle, PluginManifest } from "../plugin.js";
import { makeError } from "../error.js";
import { failure } from "../result.js";
import type { PluginResult } from "../result.js";
import { invokeLoadedApi } from "./invoke.js";
import type { LoadedApi, LoadedPlugin } from "./types.js";

function requiredArguments(api: LoadedApi): readonly string[] | null {
    const input = api.descriptor.input;
    if (
        typeof input !== "object"
        || input === null
        || !Array.isArray(input.required)
        || !input.required.every((name) => typeof name === "string")
    ) {
        return null;
    }
    return input.required;
}

function mapArguments(
    loaded: LoadedPlugin,
    api: LoadedApi,
    args: readonly unknown[],
): PluginResult<Record<string, unknown>> {
    const required = requiredArguments(api);
    const context = {
        pluginId: loaded.manifest.id,
        apiId: api.descriptor.id,
    };

    if (required === null) {
        return failure(makeError("argument_map", "protocol_error", "Positional argument mapping is unavailable",
            [{ ...context, code: "positional_mapping_unavailable" }]));
    }

    if (required.length !== args.length) {
        return failure(makeError("argument_map", "protocol_error", "Argument count does not match the API",
            [{ ...context,
                code: "argument_count_mismatch",
                expected: required.length,
                actual: args.length,
            }]));
    }

    const input: Record<string, unknown> = {};
    for (let i = 0; i < required.length; ++i) {
        input[required[i]] = args[i];
    }
    return { value: input, error: null };
}

class PluginApiHandleImpl implements PluginApiHandle {
    readonly #loaded: LoadedPlugin;
    readonly #api: LoadedApi;

    constructor(loaded: LoadedPlugin, api: LoadedApi) {
        this.#loaded = loaded;
        this.#api = api;
    }

    get id(): number { return this.#api.descriptor.id; }
    get name(): string { return this.#api.descriptor.name; }
    get input(): JsonSchema { return structuredClone(this.#api.descriptor.input); }
    get output(): JsonSchema { return structuredClone(this.#api.descriptor.output); }

    invoke(input: unknown): Promise<PluginResult<unknown>> {
        return invokeLoadedApi(this.#loaded, this.#api, input);
    }
}

export class PluginHandleImpl implements PluginHandle {
    readonly #loaded: LoadedPlugin;
    readonly #apis: readonly PluginApiHandle[];

    constructor(loaded: LoadedPlugin) {
        this.#loaded = loaded;
        this.#apis = Object.freeze(Array.from(
            loaded.apis.values(),
            (api) => new PluginApiHandleImpl(loaded, api),
        ));
    }

    get manifest(): PluginManifest { return structuredClone(this.#loaded.manifest); }
    get apis(): readonly PluginApiHandle[] { return this.#apis; }

    call(name: string, ...args: unknown[]): Promise<PluginResult<unknown>> {
        const api = Array.from(this.#loaded.apis.values()).find(
            (candidate) => candidate.descriptor.name === name,
        );
        if (api === undefined) {
            return Promise.resolve(failure(makeError("api_resolve", "protocol_error", "Plugin API was not found",
                [{ pluginId: this.#loaded.manifest.id, code: "api_not_found", value: name }])));
        }

        const mapped = mapArguments(this.#loaded, api, args);
        if (mapped.error !== null) {
            return Promise.resolve(mapped);
        }
        return invokeLoadedApi(this.#loaded, api, mapped.value);
    }
}
