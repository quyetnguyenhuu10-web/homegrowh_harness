export type PluginReference = readonly [
    type: string,
    value: string,
];

export type PluginFilesystemPermission = {
    target: string;
    access: "read_only" | "read_write";
};

export type PluginSandbox = {
    enabled: boolean;
    filesystem: PluginFilesystemPermission[];
    network: "none" | "internet_client";
};

export type PluginExecution = {
    mode: "module";
    runtimes: Array<"node" | "bun">;
    entry: string;
};

export type PluginLifecycle = {
    scope: "host" | "session" | "invocation";
};

export type JsonSchema = boolean | Record<string, unknown>;

export type PluginApi = {
    id: number;
    name: string;
    input: JsonSchema;
    output: JsonSchema;
};

export type PluginManifest = {
    id: string;
    version: string;
    api_version: number;
    execution: PluginExecution;
    lifecycle: PluginLifecycle;
    sandbox: PluginSandbox;
    references: PluginReference[];
    apis: PluginApi[];
    data: unknown;
};

export type PluginRequest = {
    api: number;
    input?: unknown;
};

export interface Plugin {
    invoke(request: PluginRequest): unknown | Promise<unknown>;
}

export type PluginModule = {
    plugin: Plugin;
};

export interface PluginApiHandle {
    readonly id: number;
    readonly name: string;
    readonly input: JsonSchema;
    readonly output: JsonSchema;

    invoke(input: unknown): Promise<unknown>;
}

export interface PluginHandle {
    readonly manifest: PluginManifest;
    readonly apis: readonly PluginApiHandle[];
}
