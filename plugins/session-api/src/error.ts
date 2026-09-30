import type { HHError, Result } from "@hh/ipc-client";

export type { HHError, Result } from "@hh/ipc-client";

export function success<T>(value: T): Result<T> {
    return { value, error: null };
}

export function failure(error: HHError): Result<never> {
    return { value: null, error };
}

export function makeError(
    operation: string,
    type: string,
    message: string,
    data: unknown[] = [],
    causes: HHError[] = [],
): HHError {
    return { source: "session_api", operation, type, message, data, causes };
}

export function combineFailures(
    operation: string,
    message: string,
    errors: readonly HHError[],
): HHError | null {
    const causes = [...new Set(errors)];
    if (causes.length === 0) return null;
    if (causes.length === 1) return causes[0]!;
    return makeError(operation, "dependency_error", message, [], causes);
}

export function isHHError(value: unknown): value is HHError {
    try {
        return hasSchema(value, new Set<object>());
    } catch {
        return false;
    }
}

function hasSchema(value: unknown, ancestors: Set<object>): boolean {
    if (typeof value !== "object" || value === null || ancestors.has(value)) return false;
    const fields = ["source", "operation", "type", "message", "data", "causes"];
    const candidate = value as Partial<HHError>;
    if (
        Object.getOwnPropertyNames(value).length !== fields.length
        || !fields.every((field) => Object.hasOwn(value, field))
        || typeof candidate.source !== "string"
        || typeof candidate.operation !== "string"
        || typeof candidate.type !== "string"
        || typeof candidate.message !== "string"
        || !Array.isArray(candidate.data)
        || !Array.isArray(candidate.causes)
    ) return false;

    ancestors.add(value);
    const valid = candidate.causes.every((cause) => hasSchema(cause, ancestors));
    ancestors.delete(value);
    return valid;
}

export function normalizeError(
    value: unknown,
    operation: string,
    type: string,
    data: unknown[] = [],
): HHError {
    return captureError(value, operation, type, data, new Set<object>());
}

function captureError(
    value: unknown,
    operation: string,
    type: string,
    data: unknown[],
    ancestors: Set<object>,
): HHError {
    if (isHHError(value)) return value;
    if (typeof value !== "object" || value === null) {
        return makeError(operation, type,
            typeof value === "string" ? value : "Failure value has no message",
            [...data, value]);
    }

    const properties: Record<string, unknown> = {};
    const causes: HHError[] = [];
    const circular = ancestors.has(value);
    ancestors.add(value);
    for (const key of Object.getOwnPropertyNames(value)) {
        let property: unknown;
        try {
            property = Reflect.get(value, key);
        } catch (error) {
            if (!circular) causes.push(captureError(error, operation, type, [], ancestors));
            properties[key] = { accessor: true };
            continue;
        }
        if (key === "cause") {
            if (!circular && property !== undefined) {
                causes.push(captureError(property, operation, type, [], ancestors));
            }
        } else if (key === "errors" && value instanceof AggregateError && Array.isArray(property)) {
            if (!circular) {
                for (const child of property) {
                    causes.push(captureError(child, operation, type, [], ancestors));
                }
            }
        } else {
            Object.defineProperty(properties, key, {
                value: property, enumerable: true, configurable: true, writable: true,
            });
        }
    }
    if (value instanceof Error && properties.name === undefined) {
        try {
            properties.name = value.name;
        } catch (error) {
            if (!circular) causes.push(captureError(error, operation, type, [], ancestors));
            properties.name = { accessor: true };
        }
    }
    if (circular) properties.circular_reference = true;
    else ancestors.delete(value);

    return makeError(operation, type,
        typeof properties.message === "string" ? properties.message : "Failure value has no message",
        [...data, properties], causes);
}
