import { Ajv2020 } from "ajv/dist/2020.js";
import type { AnySchema } from "ajv";
import type { PluginManifest } from "../plugin.js";
import { captureError } from "../error.js";
import { failure, success } from "../result.js";
import type { PluginResult } from "../result.js";
import type { LoadedApi } from "./types.js";

const uint64Max = (1n << 64n) - 1n;

function validHhInteger(type: string, value: unknown): boolean {
    const integer = typeof value === "bigint"
        ? value
        : typeof value === "number" && Number.isSafeInteger(value)
            ? BigInt(value)
            : null;
    if (integer === null || integer < 0n || integer > uint64Max) {
        return false;
    }
    return type === "uint64" || (type === "uint64-positive" && integer > 0n);
}

export function createValidator(): Ajv2020 {
    const ajv = new Ajv2020({ allErrors: true, strict: true });
    ajv.addKeyword({
        keyword: "x-hh-type",
        schemaType: "string",
        metaSchema: { enum: ["uint64", "uint64-positive"] },
        errors: false,
        validate: validHhInteger,
    });
    return ajv;
}

export function compileApis(
    manifest: PluginManifest, context: Record<string, unknown> = {},
): PluginResult<Map<number, LoadedApi>> {
    let apiId: number | undefined;
    let direction: "input" | "output" | undefined;
    try {
        const ajv = createValidator();
        const apis = new Map<number, LoadedApi>();
        for (const api of manifest.apis) {
            apiId = api.id;
            direction = "input";
            const validateInput = ajv.compile(api.input as AnySchema);
            direction = "output";
            const validateOutput = ajv.compile(api.output as AnySchema);
            apis.set(api.id, { descriptor: api, validateInput, validateOutput });
        }
        return success(apis);
    } catch (cause) {
        return failure(captureError("schema_compile", cause, {
            ...context, pluginId: manifest.id,
            ...(apiId === undefined ? {} : { apiId }),
            ...(direction === undefined ? {} : { direction }),
        }));
    }
}

