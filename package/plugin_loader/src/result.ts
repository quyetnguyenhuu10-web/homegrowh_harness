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

export type HHError = {
    source: string;
    operation: string;
    type: string;
    message: string;
    data: unknown[];
    causes: HHError[];
};

export type Result<T> =
    | { value: T; error: null }
    | { value: null; error: HHError };

// Compatibility names refer to the same universal schema.
export type PluginLoaderError = HHError;
export type PluginResult<T> = Result<T>;

export type PluginErrorSource = {
    readonly error: HHError | null;
};

export type PluginErrorInput = {
    result: PluginErrorSource;
};

/** Read one operation's error without copying or consuming it. */
export function get_error(input: PluginErrorInput): HHError | null {
    return input.result.error;
}

export function success<T>(value: T): PluginResult<T> {
    return { value, error: null };
}

export function failure(error: HHError): Result<never> {
    return { value: null, error };
}
