import { failure, makeError, success, type Result } from "../error.js";

export function endpoint(name: string, operation: string): Result<string> {
    if (
        typeof name !== "string" ||
        name.length === 0 ||
        name.includes("\0") ||
        name.includes("/") ||
        name.includes("\\")
    ) {
        return failure(makeError(
            operation,
            "validation_error",
            "IPC name is invalid",
            [{ name }],
        ));
    }

    return success(process.platform === "win32"
        ? `\\\\.\\pipe\\${name}`
        : `/tmp/${name}.sock`);
}
