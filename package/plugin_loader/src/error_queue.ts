import { normalize_error } from "./error.js";
import type { ErrorContext } from "./error.js";
import type { HHError } from "./result.js";

export class PluginErrorQueue {
    #errors: HHError[] = [];
    readonly #context: ErrorContext;

    constructor(context: ErrorContext = { source: "plugin_loader", operation: "invoke" }) {
        this.#context = context;
    }

    push(error: unknown, context: Partial<ErrorContext> = {}): HHError {
        const normalized = normalize_error(error, { ...this.#context, ...context });
        this.#errors.push(normalized);
        return normalized;
    }

    drain(): HHError[] {
        const errors = this.#errors;
        this.#errors = [];
        return errors;
    }

    get size(): number {
        return this.#errors.length;
    }
}
