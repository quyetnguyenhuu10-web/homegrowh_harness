/** Preserve non-JSON values with explicit types instead of dropping/coercing them. */
export function encodeValue(
    value: unknown,
    references = new WeakMap<object, string>(),
    location = "$",
): unknown {
    if (value === null || typeof value === "string" || typeof value === "boolean") {
        return value;
    }
    if (typeof value === "number") {
        return Number.isFinite(value)
            ? value : { value_type: "number", value: String(value) };
    }
    if (typeof value === "undefined") {
        return { value_type: "undefined" };
    }
    if (typeof value === "bigint") {
        return { value_type: "bigint", value: value.toString() };
    }
    if (typeof value === "symbol") {
        return { value_type: "symbol", description: value.description ?? null,
            key: Symbol.keyFor(value) ?? null };
    }
    if (typeof value === "function") {
        return { value_type: "function", source: Function.prototype.toString.call(value) };
    }
    const object = value as object;
    const reference = references.get(object);
    if (reference !== undefined) {
        return { $ref: reference };
    }
    references.set(object, location);
    if (Array.isArray(value)) {
        return Array.from(value, (item, index) => encodeValue(item, references, `${location}/${index}`));
    }
    if (value instanceof Date) {
        const time = Date.prototype.getTime.call(value);
        return { value_type: "Date", value: Number.isFinite(time)
            ? Date.prototype.toISOString.call(value) : null };
    }
    if (value instanceof Map) {
        return { value_type: "Map", entries: Array.from(value.entries(), ([key, item], index) => [
            encodeValue(key, references, `${location}/entries/${index}/0`),
            encodeValue(item, references, `${location}/entries/${index}/1`),
        ]) };
    }
    if (value instanceof Set) {
        return { value_type: "Set", values: Array.from(value.values(), (item, index) =>
            encodeValue(item, references, `${location}/values/${index}`)) };
    }
    const output: Record<string, unknown> = {};
    for (const [key, descriptor] of Object.entries(Object.getOwnPropertyDescriptors(object))) {
        Object.defineProperty(output, key, {
            value: "value" in descriptor
                ? encodeValue(descriptor.value, references, `${location}/${key}`)
                : { value_type: "accessor",
                    get: descriptor.get === undefined ? null : Function.prototype.toString.call(descriptor.get),
                    set: descriptor.set === undefined ? null : Function.prototype.toString.call(descriptor.set) },
            enumerable: true, configurable: true, writable: true,
        });
    }
    return output;
}
