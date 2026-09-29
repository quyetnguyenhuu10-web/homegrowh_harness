import { readFile, readdir } from "node:fs/promises";
import path from "node:path";
import { pathToFileURL } from "node:url";

import { Ajv2020 } from "ajv/dist/2020.js";
import type {
    AnySchema,
    ValidateFunction,
} from "ajv";

import type {
    JsonSchema,
    PluginApi,
    PluginApiHandle,
    PluginExecution,
    PluginFilesystemPermission,
    PluginHandle,
    PluginLifecycle,
    Plugin,
    PluginManifest,
    PluginModule,
    PluginSandbox,
} from "./plugin.js";

const manifestFileName = "plugin.json";
const ajv = new Ajv2020({
    allErrors: true,
    strict: true,
});

const uint64Max = (1n << 64n) - 1n;

function validHhInteger(type: string, value: unknown): boolean {
    let integer: bigint;

    if (typeof value === "bigint") {
        integer = value;
    } else if (
        typeof value === "number"
        && Number.isSafeInteger(value)
    ) {
        integer = BigInt(value);
    } else {
        return false;
    }

    if (integer < 0n || integer > uint64Max) {
        return false;
    }

    if (type === "uint64") {
        return true;
    }
    if (type === "uint64-positive") {
        return integer > 0n;
    }
    return false;
}

ajv.addKeyword({
    keyword: "x-hh-type",
    schemaType: "string",
    metaSchema: {
        enum: [
            "uint64",
            "uint64-positive",
        ],
    },
    errors: false,
    validate: validHhInteger,
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
        "runtimes",
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
    if (!Array.isArray(executionValue.runtimes)) {
        throw new Error(
            "plugin manifest execution.runtimes must be an array",
        );
    }

    if (executionValue.runtimes.length === 0) {
        throw new Error(
            "plugin manifest execution.runtimes must not be empty",
        );
    }

    const runtimes: Array<"node" | "bun"> = [];
    const seenRuntimes = new Set<string>();
    for (const runtime of executionValue.runtimes) {
        if (runtime !== "node" && runtime !== "bun") {
            throw new Error(
                "plugin manifest execution.runtimes contains unsupported runtime",
            );
        }
        if (seenRuntimes.has(runtime)) {
            throw new Error(
                "plugin manifest execution.runtimes must not contain duplicates",
            );
        }
        seenRuntimes.add(runtime);
        runtimes.push(runtime);
    }

    const execution: PluginExecution = {
        mode: executionValue.mode,
        runtimes,
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
        const apiKeys = new Set([
            "id",
            "name",
            "input",
            "output",
        ]);
        for (const key of Object.keys(api)) {
            if (!apiKeys.has(key)) {
                throw new Error(
                    "plugin manifest apis[" + index
                    + "] field is unsupported: " + key,
                );
            }
        }

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

function schemaSnapshot(schema: JsonSchema): JsonSchema {
    return structuredClone(schema);
}

async function invokeLoadedApi(
    loaded: LoadedPlugin,
    api: LoadedApi,
    input: unknown,
): Promise<unknown> {
    if (!api.validateInput(input)) {
        throw validationError(
            loaded.manifest.id,
            api.descriptor,
            "input",
            api.validateInput,
        );
    }

    const output = await loaded.plugin.invoke({
        api: api.descriptor.id,
        input,
    });

    if (!api.validateOutput(output)) {
        throw validationError(
            loaded.manifest.id,
            api.descriptor,
            "output",
            api.validateOutput,
        );
    }

    return output;
}

class PluginApiHandleImpl implements PluginApiHandle {
    readonly #loaded: LoadedPlugin;
    readonly #api: LoadedApi;

    constructor(loaded: LoadedPlugin, api: LoadedApi) {
        this.#loaded = loaded;
        this.#api = api;
    }

    get id(): number {
        return this.#api.descriptor.id;
    }

    get name(): string {
        return this.#api.descriptor.name;
    }

    get input(): JsonSchema {
        return schemaSnapshot(this.#api.descriptor.input);
    }

    get output(): JsonSchema {
        return schemaSnapshot(this.#api.descriptor.output);
    }

    invoke(input: unknown): Promise<unknown> {
        return invokeLoadedApi(this.#loaded, this.#api, input);
    }
}

class PluginHandleImpl implements PluginHandle {
    readonly #loaded: LoadedPlugin;
    readonly #apis: readonly PluginApiHandle[];

    constructor(loaded: LoadedPlugin) {
        this.#loaded = loaded;
        this.#apis = Object.freeze(
            Array.from(
                loaded.apis.values(),
                (api) => new PluginApiHandleImpl(loaded, api),
            ),
        );
    }

    get manifest(): PluginManifest {
        return manifestSnapshot(this.#loaded.manifest);
    }

    get apis(): readonly PluginApiHandle[] {
        return this.#apis;
    }
}

async function loadPlugin(
    manifestPath: string,
    supportedApiVersion = 1,
): Promise<LoadedPlugin> {
    const manifestRaw = await readFile(manifestPath, "utf8");
    const manifest = parseManifest(JSON.parse(manifestRaw));

    if (manifest.api_version !== supportedApiVersion) {
        throw new Error(
            "plugin " + manifest.id + " api_version "
            + manifest.api_version + " is unsupported; expected "
            + supportedApiVersion,
        );
    }

    const root = path.dirname(path.resolve(manifestPath));
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
    readonly #plugins = new Map<string, PluginHandle>();
    readonly #apiVersion: number;

    constructor(apiVersion = 1) {
        this.#apiVersion = apiVersion;
    }

    #register(loaded: LoadedPlugin): PluginHandle {
        const id = loaded.manifest.id;
        if (this.#plugins.has(id)) {
            throw new Error("plugin is already registered: " + id);
        }
        const handle = new PluginHandleImpl(loaded);
        this.#plugins.set(id, handle);
        return handle;
    }

    async load(manifestPath: string): Promise<PluginHandle> {
        const loaded = await loadPlugin(manifestPath, this.#apiVersion);
        return this.#register(loaded);
    }

    async loadAll(rootDirectory: string): Promise<PluginHandle[]> {
        const entries = await readdir(rootDirectory, {
            withFileTypes: true,
        });
        const loaded: PluginHandle[] = [];

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

            loaded.push(await this.load(manifestPath));
        }

        return loaded;
    }

    list(): PluginHandle[] {
        return Array.from(this.#plugins.values());
    }
}
