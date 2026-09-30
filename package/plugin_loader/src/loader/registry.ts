import { readdir } from "node:fs/promises";
import path from "node:path";
import type { PluginHandle } from "../plugin.js";
import { captureError, makeError } from "../error.js";
import { failure, success } from "../result.js";
import type { PluginLoaderError, PluginResult } from "../result.js";
import { PluginHandleImpl } from "./handles.js";
import { loadPlugin } from "./module.js";

export type PluginLoadAllResult = {
    /** Includes plugins registered before the first error. */
    plugins: PluginHandle[];
    error: PluginLoaderError | null;
};

export class PluginRegistry {
    readonly #plugins = new Map<string, PluginHandle>();
    readonly #apiVersion: number;

    constructor(apiVersion = 1) {
        this.#apiVersion = apiVersion;
    }

    async load(manifestPath: string): Promise<PluginResult<PluginHandle>> {
        const loaded = await loadPlugin(manifestPath, this.#apiVersion);
        if (loaded.error !== null) {
            return loaded;
        }
        const pluginId = loaded.value.manifest.id;
        try {
            if (this.#plugins.has(pluginId)) {
                return failure(makeError("register", "protocol_error", "Plugin ID is already registered",
                    [{ manifestPath, pluginId, code: "duplicate_plugin_id", value: pluginId }]));
            }
            const handle = new PluginHandleImpl(loaded.value);
            this.#plugins.set(pluginId, handle);
            return success(handle);
        } catch (cause) {
            return failure(captureError("register", cause, { manifestPath, pluginId }));
        }
    }

    async loadAll(rootDirectory: string): Promise<PluginLoadAllResult> {
        const plugins: PluginHandle[] = [];
        let manifestPath = rootDirectory;
        try {
            const entries = await readdir(rootDirectory, { withFileTypes: true });
            for (const entry of entries) {
                if (!entry.isDirectory()) {
                    continue;
                }
                manifestPath = path.join(rootDirectory, entry.name, "plugin.json");
                const result = await this.load(manifestPath);
                if (result.error !== null) {
                    // A directory without a manifest is not a plugin.
                    if (
                        result.error.operation === "manifest_read"
                        && result.error.type === "system_error"
                        && result.error.data.some((item) => typeof item === "object" && item !== null
                            && (item as { code?: unknown }).code === "ENOENT")
                    ) {
                        continue;
                    }
                    return { plugins, error: result.error };
                }
                plugins.push(result.value);
            }
            return { plugins, error: null };
        } catch (cause) {
            return {
                plugins,
                error: captureError("directory_read", cause, { rootDirectory, manifestPath }),
            };
        }
    }

    list(): PluginHandle[] {
        return Array.from(this.#plugins.values());
    }
}

