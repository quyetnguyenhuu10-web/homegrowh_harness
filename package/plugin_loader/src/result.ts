export type PluginLoaderOperation =
    | "manifest_read"
    | "manifest_parse"
    | "manifest_validate"
    | "module_resolve"
    | "module_import"
    | "module_validate"
    | "schema_compile"
    | "register"
    | "directory_read"
    | "api_resolve"
    | "argument_map"
    | "input_validate"
    | "invoke"
    | "output_validate";

export type PluginLoaderError = {
    operation: PluginLoaderOperation;
    /** Original exception, AJV errors, or structured contract violation. */
    cause: unknown;
    manifestPath?: string;
    pluginId?: string;
    apiId?: number;
    direction?: "input" | "output";
};

export type PluginResult<T> =
    | { value: T; error: null }
    | { value: null; error: PluginLoaderError };

export type PluginErrorSource = {
    readonly error: PluginLoaderError | null;
};

export type PluginErrorInput = {
    result: PluginErrorSource;
};

/** Read one operation's error without copying or consuming it. */
export function get_error(input: PluginErrorInput): PluginLoaderError | null {
    return input.result.error;
}

export function success<T>(value: T): PluginResult<T> {
    return { value, error: null };
}

export function failure(error: PluginLoaderError): PluginResult<never> {
    return { value: null, error };
}
