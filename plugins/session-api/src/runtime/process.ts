import type { ChildProcess } from "node:child_process";
import { combineFailures, failure, makeError, normalizeError, success, type HHError, type Result } from "../error.js";

export type RuntimeCloseResult = {
    code: number | null;
    signal: NodeJS.Signals | null;
};

// Owns process listeners for the full process lifetime, including late errors.
export class RuntimeProcess {
    readonly child: ChildProcess;
    readonly beforeConnect: Promise<Result<never>>;
    readonly #observers = new Set<(error: HHError) => void>();
    readonly #exitWaiters: Array<(result: Result<RuntimeCloseResult>) => void> = [];
    readonly #errors: HHError[] = [];
    readonly #startupFailure: (result: Result<never>) => void;
    readonly #terminated: Promise<void>;
    readonly #finishTermination: () => void;
    #connected = false;
    #terminating = false;
    #closed = false;
    #exit: RuntimeCloseResult | null = null;
    #error: HHError | null = null;

    constructor(child: ChildProcess) {
        this.child = child;
        let startupFailure!: (result: Result<never>) => void;
        this.beforeConnect = new Promise((resolve) => { startupFailure = resolve; });
        this.#startupFailure = startupFailure;
        let finishTermination!: () => void;
        this.#terminated = new Promise((resolve) => { finishTermination = resolve; });
        this.#finishTermination = finishTermination;
        child.on("error", this.#onError);
        child.on("exit", this.#onExit);
        child.once("close", this.#onClose);
        if (child.exitCode !== null || child.signalCode !== null) {
            this.#onExit(child.exitCode, child.signalCode);
        }
    }

    get error(): HHError | null { return this.#error; }
    get running(): boolean {
        return !this.#closed && this.#exit === null && this.child.pid !== undefined;
    }

    connected(): void { this.#connected = true; }

    onFailure(observer: (error: HHError) => void): () => void {
        this.#observers.add(observer);
        if (this.#error !== null) observer(this.#error);
        return () => { this.#observers.delete(observer); };
    }

    waitForExit(): Promise<Result<RuntimeCloseResult>> {
        if (this.#error !== null) return Promise.resolve(failure(this.#error));
        if (this.#exit !== null) return Promise.resolve(success(this.#exit));
        return new Promise((resolve) => { this.#exitWaiters.push(resolve); });
    }

    async waitForTermination(): Promise<Result<RuntimeCloseResult>> {
        if (this.running) await this.#terminated;
        return this.waitForExit();
    }

    terminate(): Result<void> {
        if (!this.running) return success(undefined);
        try {
            this.#terminating = true;
            const accepted = this.child.kill();
            if (accepted || !this.running) return success(undefined);
            this.#terminating = false;
            return failure(this.#error ?? makeError("terminate_runtime", "state_error",
                "Process termination request was not accepted",
                [{ api: "ChildProcess.kill", pid: this.child.pid, accepted }]));
        } catch (error) {
            this.#terminating = false;
            return failure(normalizeError(error, "terminate_runtime", "system_error",
                [{ api: "ChildProcess.kill", pid: this.child.pid }]));
        }
    }

    readonly #onError = (value: unknown): void => {
        const error = normalizeError(value, this.#connected ? "runtime_process" : "open_runtime",
            "system_error", [{ api: "ChildProcess", event: "error" }]);
        this.#remember(error);
        if (!this.#connected) this.#startupFailure(failure(error));
        this.#finishExitWaiters(failure(this.#error!));
    };

    readonly #onExit = (code: number | null, signal: NodeJS.Signals | null): void => {
        this.#exit = { code, signal };
        this.#finishTermination();
        if (!this.#connected && !this.#terminating) {
            const error = makeError("open_runtime", "state_error",
                "Session runtime exited before IPC connection",
                [{ api: "ChildProcess", event: "exit", exit_code: code, signal }]);
            this.#remember(error);
            this.#startupFailure(failure(error));
        } else if (!this.#terminating && (code !== 0 || signal !== null)) {
            this.#remember(makeError("runtime_process", "process_error",
                "Session runtime exited unsuccessfully",
                [{ api: "ChildProcess", event: "exit", exit_code: code, signal }]));
        }
        this.#finishExitWaiters(this.#error === null ? success(this.#exit) : failure(this.#error));
    };

    readonly #onClose = (code: number | null, signal: NodeJS.Signals | null): void => {
        this.#closed = true;
        if (this.#exit === null && this.#error === null) this.#onExit(code, signal);
        this.#finishTermination();
        this.child.off("error", this.#onError);
        this.child.off("exit", this.#onExit);
        this.#observers.clear();
    };

    #remember(error: HHError): void {
        if (this.#errors.includes(error)) return;
        this.#errors.push(error);
        this.#error = combineFailures("runtime_process", "Runtime process reported multiple failures", this.#errors);
        for (const observer of this.#observers) observer(this.#error!);
    }

    #finishExitWaiters(result: Result<RuntimeCloseResult>): void {
        for (const waiter of this.#exitWaiters.splice(0)) waiter(result);
    }
}
