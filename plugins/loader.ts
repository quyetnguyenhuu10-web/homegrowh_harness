import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

import Ajv2020 from "ajv/dist/2020.js";
import type {
    AnySchema,
    ValidateFunction,
} from "ajv";

import type {
    JsonSchema,
    PluginApi,
    PluginExecution,
    PluginFilesystemPermission,
    PluginLifecycle,
    Plugin,
    PluginManifest,
    PluginModule,
    PluginRequest,
    PluginSandbox,
} from "./plugin.ts";

const manifestFileName = "plugin.json";
const ajv = new Ajv2020({
    allErrors: true,
    strict: true,
});

type LoadedApi = {
    descriptor: PluginApi;
    validateInput: ValidateFunction;
    validateOutput: ValidateFunction;
};

type LoadedPlugin = {
    manifest: PluginManifest;
    plugin: Plugin;
    apis: Map<number, LoadedApi>;
};

function requireString(
    value: unknown,
    field: string,
    allowEmpty = false,
): string {
    if (typeof value !== "string" || (!allowEmpty && value.length === 0)) {
        throw new Error(
            allowEmpty
                ? "plugin manifest " + field + " must be a string"
                : "plugin manifest " + field + " must be a non-empty string",
        );
    }
    return value;
}

function requireSchema(value: unknown, field: string): JsonSchema {
    if (
        typeof value !== "boolean"
        && (
            typeof value !== "object"
            || value === null
            || Array.isArray(value)
        )
    ) {
        throw new Error(
            "plugin manifest " + field
            + " must be a JSON Schema object or boolean",
        );
    }
    return value as JsonSchema;
}

function parseManifest(value: unknown): PluginManifest {
    if (typeof value !== "object" || value === null || Array.isArray(value)) {
        throw new Error("plugin manifest must be an object");
    }

    const manifest = value as Record<string, unknown>;
    const allowed = new Set([
        "id",
        "version",
        "api_version",
        "execution",
        "lifecycle",
        "sandbox",
        "references",
        "apis",
        "data",
    ]);

    for (const key of Object.keys(manifest)) {
        if (!allowed.has(key)) {
            throw new Error(
                "plugin manifest field is unsupported: " + key,
            );
        }
    }

    const apiVersion = manifest.api_version;
    if (
        typeof apiVersion !== "number"
        || !Number.isInteger(apiVersion)
        || apiVersion < 1
    ) {
        throw new Error(
            "plugin manifest api_version must be an integer >= 1",
        );
    }

    if (
        typeof manifest.execution !== "object"
        || manifest.execution === null
        || Array.isArray(manifest.execution)
    ) {
        throw new Error("plugin manifest execution must be an object");
    }

    const executionValue = manifest.execution as Record<string, unknown>;
    const executionKeys = new Set([
        "mode",
        "runtime",
        "entry",
    ]);
    for (const key of Object.keys(executionValue)) {
        if (!executionKeys.has(key)) {
            throw new Error(
                "plugin manifest execution field is unsupported: " + key,
            );
        }
    }

    if (executionValue.mode !== "module") {
        throw new Error(
            "plugin manifest execution.mode must be module",
        );
    }
    if (executionValue.runtime !== "bun") {
        throw new Error(
            "plugin manifest execution.runtime must be bun",
        );
    }

    const execution: PluginExecution = {
        mode: executionValue.mode,
        runtime: executionValue.runtime,
        entry: requireString(
            executionValue.entry,
            "execution.entry",
        ),
    };

    if (
        typeof manifest.lifecycle !== "object"
        || manifest.lifecycle === null
        || Array.isArray(manifest.lifecycle)
    ) {
        throw new Error("plugin manifest lifecycle must be an object");
    }

    const lifecycleValue = manifest.lifecycle as Record<string, unknown>;
    const lifecycleKeys = new Set(["scope"]);
    for (const key of Object.keys(lifecycleValue)) {
        if (!lifecycleKeys.has(key)) {
            throw new Error(
                "plugin manifest lifecycle field is unsupported: " + key,
            );
        }
    }

    if (
        lifecycleValue.scope !== "host"
        && lifecycleValue.scope !== "session"
        && lifecycleValue.scope !== "invocation"
    ) {
        throw new Error(
            "plugin manifest lifecycle.scope must be host, session, or invocation",
        );
    }

    const lifecycle: PluginLifecycle = {
        scope: lifecycleValue.scope,
    };

    if (
        typeof manifest.sandbox !== "object"
        || manifest.sandbox === null
        || Array.isArray(manifest.sandbox)
    ) {
        throw new Error("plugin manifest sandbox must be an object");
    }

    const sandboxValue = manifest.sandbox as Record<string, unknown>;
    const sandboxKeys = new Set([
        "enabled",
        "filesystem",
        "network",
    ]);
    for (const key of Object.keys(sandboxValue)) {
        if (!sandboxKeys.has(key)) {
            throw new Error(
                "plugin manifest sandbox field is unsupported: " + key,
            );
        }
    }

    if (typeof sandboxValue.enabled !== "boolean") {
        throw new Error(
            "plugin manifest sandbox.enabled must be a boolean",
        );
    }

    if (!Array.isArray(sandboxValue.filesystem)) {
        throw new Error(
            "plugin manifest sandbox.filesystem must be an array",
        );
    }

    const filesystem: PluginFilesystemPermission[] =
        sandboxValue.filesystem.map((value, index) => {
            if (
                typeof value !== "object"
                || value === null
                || Array.isArray(value)
            ) {
                throw new Error(
                    "plugin manifest sandbox.filesystem[" + index
                    + "] must be an object",
                );
            }

            const permission = value as Record<string, unknown>;
            const keys = new Set(["target", "access"]);
            for (const key of Object.keys(permission)) {
                if (!keys.has(key)) {
                    throw new Error(
                        "plugin manifest sandbox.filesystem[" + index
                        + "] field is unsupported: " + key,
                    );
                }
            }

            const target = requireString(
                permission.target,
                "sandbox.filesystem[" + index + "].target",
            );
            if (
                permission.access !== "read_only"
                && permission.access !== "read_write"
            ) {
                throw new Error(
                    "plugin manifest sandbox.filesystem[" + index
                    + "].access must be read_only or read_write",
                );
            }

            return {
                target,
                access: permission.access,
            };
        });

    if (
        sandboxValue.network !== "none"
        && sandboxValue.network !== "internet_client"
    ) {
        throw new Error(
            "plugin manifest sandbox.network must be none or internet_client",
        );
    }

    const sandbox: PluginSandbox = {
        enabled: sandboxValue.enabled,
        filesystem,
        network: sandboxValue.network,
    };

    if (!Array.isArray(manifest.references)) {
        throw new Error("plugin manifest references must be an array");
    }

    const references = manifest.references.map((reference, index) => {
        if (!Array.isArray(reference) || reference.length !== 2) {
            throw new Error(
                "plugin manifest references[" + index
                + "] must contain [type, value]",
            );
        }

        return [
            requireString(reference[0], "references[" + index + "][0]"),
            requireString(
                reference[1],
                "references[" + index + "][1]",
                true,
            ),
        ] as const;
    });

    if (!Array.isArray(manifest.apis)) {
        throw new Error("plugin manifest apis must be an array");
    }

    const apiIds = new Set<number>();
    const apiNames = new Set<string>();
    const apis: PluginApi[] = manifest.apis.map((value, index) => {
        if (
            typeof value !== "object"
            || value === null
            || Array.isArray(value)
        ) {
            throw new Error(
                "plugin manifest apis[" + index + "] must be an object",
            );
        }

        const api = value as Record<string, unknown>;
        const id = api.id;
        if (
            typeof id !== "number"
            || !Number.isSafeInteger(id)
            || id <= 0
        ) {
            throw new Error(
                "plugin manifest apis[" + index
                + "].id must be a positive safe integer",
            );
        }
        if (apiIds.has(id)) {
            throw new Error(
                "plugin manifest api id is duplicated: " + id,
            );
        }
        apiIds.add(id);

        const name = requireString(api.name, "apis[" + index + "].name");
        if (apiNames.has(name)) {
            throw new Error(
                "plugin manifest api name is duplicated: " + name,
            );
        }
        apiNames.add(name);

        const description = requireString(
            api.description,
            "apis[" + index + "].description",
        );

        if (!Object.prototype.hasOwnProperty.call(api, "input")) {
            throw new Error(
                "plugin manifest apis[" + index + "].input is required",
            );
        }
        if (!Object.prototype.hasOwnProperty.call(api, "output")) {
            throw new Error(
                "plugin manifest apis[" + index + "].output is required",
            );
        }

        return {
            id,
            name,
            description,
            input: requireSchema(
                api.input,
                "apis[" + index + "].input",
            ),
            output: requireSchema(
                api.output,
                "apis[" + index + "].output",
            ),
        };
    });

    if (!Object.prototype.hasOwnProperty.call(manifest, "data")) {
        throw new Error("plugin manifest data is required");
    }

    return {
        id: requireString(manifest.id, "id"),
        version: requireString(manifest.version, "version"),
        api_version: apiVersion,
        execution,
        lifecycle,
        sandbox,
        references,
        apis,
        data: manifest.data,
    };
}

function parsePluginModule(value: unknown, id: string): PluginModule {
    if (typeof value !== "object" || value === null) {
        throw new Error("plugin " + id + " entry must export a module object");
    }

    const module = value as Record<string, unknown>;
    const plugin = module.plugin;
    if (
        typeof plugin !== "object"
        || plugin === null
        || typeof (plugin as { invoke?: unknown }).invoke !== "function"
    ) {
        throw new Error(
            "plugin " + id
            + " must export const plugin with invoke(request)",
        );
    }

    return { plugin: plugin as Plugin };
}

function compileValidator(
    schema: JsonSchema,
    pluginId: string,
    api: PluginApi,
    direction: "input" | "output",
): ValidateFunction {
    try {
        return ajv.compile(schema as AnySchema);
    } catch (error) {
        throw new Error(
            "plugin " + pluginId
            + " api " + api.id + " (" + api.name + ") "
            + direction + " schema is invalid",
            { cause: error },
        );
    }
}

function compileApis(manifest: PluginManifest): Map<number, LoadedApi> {
    const result = new Map<number, LoadedApi>();

    for (const api of manifest.apis) {
        result.set(api.id, {
            descriptor: api,
            validateInput: compileValidator(
                api.input,
                manifest.id,
                api,
                "input",
            ),
            validateOutput: compileValidator(
                api.output,
                manifest.id,
                api,
                "output",
            ),
        });
    }

    return result;
}

function validationError(
    pluginId: string,
    api: PluginApi,
    direction: "input" | "output",
    validator: ValidateFunction,
): Error {
    return new Error(
        "plugin " + pluginId
        + " api " + api.id + " (" + api.name + ") "
        + direction + " validation failed: "
        + ajv.errorsText(validator.errors, {
            separator: "; ",
        }),
    );
}

function manifestSnapshot(manifest: PluginManifest): PluginManifest {
    return structuredClone(manifest);
}

async function loadPlugin(
    directory: string,
    supportedApiVersion = 1,
): Promise<LoadedPlugin> {
    const manifestPath = path.join(directory, manifestFileName);
    const manifestRaw = await readFile(manifestPath, "utf8");
    const manifest = parseManifest(JSON.parse(manifestRaw));

    if (manifest.api_version !== supportedApiVersion) {
        throw new Error(
            "plugin " + manifest.id + " api_version "
            + manifest.api_version + " is unsupported; expected "
            + supportedApiVersion,
        );
    }

    const root = path.resolve(directory);
    const entryPath = path.resolve(root, manifest.execution.entry);
    const relativeEntry = path.relative(root, entryPath);
    if (
        relativeEntry === ".."
        || relativeEntry.startsWith(".." + path.sep)
        || path.isAbsolute(relativeEntry)
    ) {
        throw new Error(
            "plugin " + manifest.id
            + " execution.entry must remain inside its plugin directory",
        );
    }

    const loaded = await import(pathToFileURL(entryPath).href);
    const module = parsePluginModule(loaded, manifest.id);

    return {
        manifest,
        plugin: module.plugin,
        apis: compileApis(manifest),
    };
}

export class PluginRegistry {
    readonly #plugins = new Map<string, LoadedPlugin>();
    readonly #apiVersion: number;

    constructor(apiVersion = 1) {
        this.#apiVersion = apiVersion;
    }

    #register(loaded: LoadedPlugin): void {
        const id = loaded.manifest.id;
        if (this.#plugins.has(id)) {
            throw new Error("plugin is already registered: " + id);
        }
        this.#plugins.set(id, loaded);
    }

    async load(directory: string): Promise<PluginManifest> {
        const loaded = await loadPlugin(directory, this.#apiVersion);
        this.#register(loaded);
        return manifestSnapshot(loaded.manifest);
    }

    async loadAll(rootDirectory: string): Promise<PluginManifest[]> {
        const entries = await readdir(rootDirectory, {
            withFileTypes: true,
        });
        const loaded: PluginManifest[] = [];

        for (const entry of entries) {
            if (!entry.isDirectory()) {
                continue;
            }

            const directory = path.join(rootDirectory, entry.name);
            const manifestPath = path.join(directory, manifestFileName);

            try {
                await readFile(manifestPath, "utf8");
            } catch {
                continue;
            }

            loaded.push(await this.load(directory));
        }

        return loaded;
    }

    #get(id: string): LoadedPlugin {
        const loaded = this.#plugins.get(id);
        if (loaded === undefined) {
            throw new Error("plugin is not registered: " + id);
        }
        return loaded;
    }

    list(): PluginManifest[] {
        return Array.from(
            this.#plugins.values(),
            (loaded) => manifestSnapshot(loaded.manifest),
        );
    }

    describe(id: string): PluginManifest {
        return manifestSnapshot(this.#get(id).manifest);
    }

    async invoke(
        id: string,
        request: PluginRequest,
    ): Promise<unknown> {
        const loaded = this.#get(id);
        const api = loaded.apis.get(request.api);
        if (api === undefined) {
            throw new Error(
                "plugin " + id + " does not declare api id " + request.api,
            );
        }

        if (!api.validateInput(request.input)) {
            throw validationError(
                id,
                api.descriptor,
                "input",
                api.validateInput,
            );
        }

        const output = await loaded.plugin.invoke(request);

        if (!api.validateOutput(output)) {
            throw validationError(
                id,
                api.descriptor,
                "output",
                api.validateOutput,
            );
        }

        return output;
    }
}
