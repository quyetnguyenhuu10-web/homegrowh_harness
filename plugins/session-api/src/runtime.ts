import { RuntimeHost } from "./runtime/host.js";
export type { CommandResult } from "./runtime/entry.js";
export type { RuntimeCloseResult } from "./runtime/process.js";
import type { UInt64 } from "./commands.js";

const host = new RuntimeHost();

export function openRuntime(executable: string) { return host.openRuntime(executable); }
export function sendRuntime(runtimeId: UInt64, message: unknown) { return host.sendRuntime(runtimeId, message); }
export function receiveRuntime(runtimeId: UInt64) { return host.receiveRuntime(runtimeId); }
export function streamRuntimeEvent(runtimeId: UInt64) { return host.streamRuntimeEvent(runtimeId); }
export function streamRuntimeError(runtimeId: UInt64) { return host.streamRuntimeError(runtimeId); }
export function executeRuntimeCommand(runtimeId: UInt64, commandId: UInt64, message: unknown) {
    return host.executeRuntimeCommand(runtimeId, commandId, message);
}
export function closeRuntime(runtimeId: UInt64) { return host.closeRuntime(runtimeId); }
