import type { HHError } from "./result.js";
import { encodeValue } from "./error/values.js";

export type ErrorContext = {
    source: string;
    operation: string;
    data?: unknown[];
};

const fields = ["source", "operation", "type", "message", "data", "causes"] as const;

/** Only the fixed envelope is an HHError; payloads stay open. */
export function is_hh_error(value: unknown): value is HHError {
    const ancestors = new Set<object>();
    function check(candidate: unknown): boolean {
        if (typeof candidate !== "object" || candidate === null || ancestors.has(candidate)) {
            return false;
        }
        const descriptors = Object.getOwnPropertyDescriptors(candidate);
        if (Reflect.ownKeys(descriptors).length !== fields.length
            || !fields.every((field) => descriptors[field] !== undefined && "value" in descriptors[field])) {
            return false;
        }
        if (!fields.slice(0, 4).every((field) => typeof descriptors[field].value === "string")
            || !Array.isArray(descriptors.data.value) || !Array.isArray(descriptors.causes.value)) {
            return false;
        }
        ancestors.add(candidate);
        const causes = descriptors.causes.value as unknown[];
        let valid = true;
        for (let i = 0; i < causes.length; ++i) {
            if (!check(causes[i])) {
                valid = false;
                break;
            }
        }
        ancestors.delete(candidate);
        return valid;
    }
    try {
        return check(value);
    } catch {
        return false;
    }
}

export function makeError(
    operation: string, type: string, message: string, data: unknown[] = [],
    causes: HHError[] = [],
): HHError {
    return { source: "plugin_loader", operation, type, message, data, causes };
}

/** Normalize at the native boundary. An existing HHError is forwarded by reference. */
export function normalize_error(value: unknown, context: ErrorContext): HHError {
    const ancestors = new WeakMap<object, string>();
    function normalize(thrown: unknown, location: string): HHError {
        if (is_hh_error(thrown)) {
            return thrown;
        }
        const error: HHError = {
            source: context.source, operation: context.operation, type: "exception",
            message: typeof thrown === "string" ? thrown : "Thrown value",
            data: [], causes: [],
        };
        if (context.data !== undefined) {
            error.data.push(...context.data);
        }
        function addCause(cause: unknown, field: string): void {
            const reference = typeof cause === "object" && cause !== null
                ? ancestors.get(cause) : undefined;
            if (reference !== undefined) {
                error.data.push({ field, $ref: reference });
            } else {
                error.causes.push(normalize(cause, `${location}/${field}`));
            }
        }
        try {
            if (typeof thrown === "object" && thrown !== null) {
                ancestors.set(thrown, location);
            }
            if (typeof thrown !== "object" || thrown === null || Array.isArray(thrown)
                || thrown instanceof Date || thrown instanceof Map || thrown instanceof Set) {
                error.data.unshift(encodeValue(thrown));
                return error;
            }
            const descriptors = Object.getOwnPropertyDescriptors(thrown);
            const details: Record<string, unknown> = {};
            error.data.unshift(details);
            let prototype: object | null = thrown;
            const prototypes = new Set<object>();
            while (prototype !== null && !prototypes.has(prototype)) {
                prototypes.add(prototype);
                const name = Object.getOwnPropertyDescriptor(prototype, "name");
                if (name !== undefined) {
                    if ("value" in name) {
                        details.name = encodeValue(name.value);
                    }
                    break;
                }
                prototype = Object.getPrototypeOf(prototype);
            }
            const message = descriptors.message;
            if (message !== undefined && "value" in message && typeof message.value === "string") {
                error.message = message.value;
            }
            if (thrown instanceof SyntaxError) {
                error.type = "protocol_error";
            } else if (typeof descriptors.errno?.value === "number"
                && typeof descriptors.syscall?.value === "string") {
                error.type = "system_error";
            } else if (thrown instanceof AggregateError) {
                error.type = "aggregate_error";
            }
            for (const [key, descriptor] of Object.entries(descriptors)) {
                if (key === "cause" && "value" in descriptor) {
                    addCause(descriptor.value, "cause");
                } else if (key === "errors" && thrown instanceof AggregateError && "value" in descriptor
                    && Array.isArray(descriptor.value)) {
                    Array.prototype.forEach.call(descriptor.value,
                        (cause: unknown, index: number) => addCause(cause, `errors/${index}`));
                } else {
                    let encoded = "value" in descriptor ? encodeValue(descriptor.value)
                        : { value_type: "accessor",
                            get: descriptor.get === undefined ? null
                                : Function.prototype.toString.call(descriptor.get),
                            set: descriptor.set === undefined ? null
                                : Function.prototype.toString.call(descriptor.set) };
                    // V8 exposes native stacks lazily. Capture their content, while
                    // leaving application accessors unexecuted.
                    if (key === "stack" && thrown instanceof Error && descriptor.get !== undefined
                        && Function.prototype.toString.call(descriptor.get).includes("[native code]")) {
                        try {
                            encoded = encodeValue(Reflect.apply(descriptor.get, thrown, []));
                        } catch (stackError) {
                            addCause(stackError, "stack");
                        }
                    }
                    Object.defineProperty(details, key, {
                        value: encoded,
                        enumerable: true, configurable: true, writable: true,
                    });
                }
            }
        } catch (inspectionError) {
            error.data.unshift({ value_type: typeof thrown, api: "capture_error_metadata" });
            addCause(inspectionError, "metadata");
        } finally {
            if (typeof thrown === "object" && thrown !== null) {
                ancestors.delete(thrown);
            }
        }
        return error;
    }
    return normalize(value, "$");
}

/** Loader API context is a new semantic layer; the lower error stays intact. */
export function captureError(
    operation: string, value: unknown, context: Record<string, unknown> = {},
): HHError {
    if (is_hh_error(value)) {
        if (Object.keys(context).length === 0
            || (value.source === "plugin_loader" && value.operation === operation
                && Object.entries(context).every(([key, entry]) => value.data.some((item) => {
                    if (typeof item !== "object" || item === null) return false;
                    const descriptor = Object.getOwnPropertyDescriptor(item, key);
                    return descriptor !== undefined && "value" in descriptor && Object.is(descriptor.value, entry);
                })))) {
            return value;
        }
        return makeError(operation, "dependency_error", "Plugin operation failed",
            [context], [value]);
    }
    return normalize_error(value, {
        source: "plugin_loader", operation,
        data: Object.keys(context).length === 0 ? [] : [context],
    });
}
