import { readFile } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";
import type { Plugin } from "../plugin.js";
import { failure, success } from "../result.js";
import type { PluginLoaderOperation, PluginResult } from "../result.js";
import { parseManifest } from "./manifest.js";
import { compileApis } from "./schema.js";
import type { LoadedPlugin } from "./types.js";

export async function loadPlugin(
    manifestPath: string,
    supportedApiVersion: number,
): Promise<PluginResult<LoadedPlugin>> {
    let operation: PluginLoaderOperation = "manifest_read";
    let pluginId: string | undefined;
    try {
        const raw = await readFile(manifestPath, "utf8");
        operation = "manifest_parse";
        const parsed = parseManifest(JSON.parse(raw));
        if (parsed.error !== null) {
            return failure({ ...parsed.error, manifestPath });
        }

        const manifest = parsed.value;
        pluginId = manifest.id;
        if (manifest.api_version !== supportedApiVersion) {
            return failure({
                operation: "manifest_validate", manifestPath, pluginId,
                cause: {
                    code: "unsupported_api_version",
                    value: manifest.api_version,
                    expected: supportedApiVersion,
                },
            });
        }

        operation = "module_resolve";
        const root = path.dirname(path.resolve(manifestPath));
        const entryPath = path.resolve(root, manifest.execution.entry);
        const relativeEntry = path.relative(root, entryPath);
        if (
            relativeEntry === ".."
            || relativeEntry.startsWith(".." + path.sep)
            || path.isAbsolute(relativeEntry)
        ) {
            return failure({
                operation, manifestPath, pluginId,
                cause: { code: "entry_outside_plugin_directory", entryPath, root },
            });
        }

        operation = "module_import";
        const module: unknown = await import(pathToFileURL(entryPath).href);
        operation = "module_validate";
        const plugin = typeof module === "object" && module !== null
            ? (module as { plugin?: unknown }).plugin
            : undefined;
        if (
            typeof plugin !== "object"
            || plugin === null
            || typeof (plugin as { invoke?: unknown }).invoke !== "function"
        ) {
            return failure({
                operation, manifestPath, pluginId,
                cause: { code: "invalid_plugin_export", expected: "plugin.invoke(request)" },
            });
        }

        const apis = compileApis(manifest);
        if (apis.error !== null) {
            return failure({ ...apis.error, manifestPath });
        }
        return success({ manifest, plugin: plugin as Plugin, apis: apis.value });
    } catch (cause) {
        return failure({ operation, cause, manifestPath, pluginId });
    }
}

