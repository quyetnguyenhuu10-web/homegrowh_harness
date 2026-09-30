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
    return {
        source: "ipc_client",
        operation,
        type,
        message,
        data,
        causes,
    };
}

export function isHHError(value: unknown): value is HHError {
    try {
        return hasErrorSchema(value, new Set<object>());
    } catch {
        // Accessor failures are captured when normalizing the native properties.
        return false;
    }
}

function hasErrorSchema(value: unknown, ancestors: Set<object>): boolean {
    if (typeof value !== "object" || value === null || ancestors.has(value)) {
        return false;
    }

    const candidate = value as Partial<HHError>;
    const fields = ["source", "operation", "type", "message", "data", "causes"];
    if (
        Object.getOwnPropertyNames(value).length !== fields.length ||
        !fields.every((field) => Object.hasOwn(value, field)) ||
        typeof candidate.source !== "string" ||
        typeof candidate.operation !== "string" ||
        typeof candidate.type !== "string" ||
        typeof candidate.message !== "string" ||
        !Array.isArray(candidate.data) ||
        !Array.isArray(candidate.causes)
    ) {
        return false;
    }

    ancestors.add(value);
    const valid = candidate.causes.every((cause) => hasErrorSchema(cause, ancestors));
    ancestors.delete(value);
    return valid;
}

// Native properties stay in data; native cause/AggregateError.errors become causes.
// An existing HHError is forwarded by reference without changing its context.
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
    if (isHHError(value)) {
        return value;
    }

    if (typeof value !== "object" || value === null) {
        return makeError(
            operation,
            type,
            typeof value === "string" ? value : "Failure value has no message",
            [...data, value],
        );
    }

    const properties: Record<string, unknown> = {};
    const causes: HHError[] = [];
    const circular = ancestors.has(value);
    ancestors.add(value);

    for (const key of Object.getOwnPropertyNames(value)) {
        const descriptor = Object.getOwnPropertyDescriptor(value, key);
        let property: unknown;
        try {
            property = Reflect.get(value, key);
        } catch (error) {
            // A failing accessor is itself an observed failure, never a guessed value.
            if (!circular) {
                causes.push(captureError(error, operation, type, [], ancestors));
            }
            properties[key] = { accessor: true };
            continue;
        }

        if (key === "cause" && descriptor !== undefined) {
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
                value: property,
                enumerable: true,
                configurable: true,
                writable: true,
            });
        }
    }

    if (value instanceof Error && properties.name === undefined) {
        try {
            properties.name = value.name;
        } catch (error) {
            if (!circular) {
                causes.push(captureError(error, operation, type, [], ancestors));
            }
            properties.name = { accessor: true };
        }
    }
    if (circular) {
        properties.circular_reference = true;
    } else {
        ancestors.delete(value);
    }

    return makeError(
        operation,
        type,
        typeof properties.message === "string" ? properties.message : "Failure value has no message",
        [...data, properties],
        causes,
    );
}
