export type SandboxFilesystemAccess = "read_only" | "read_write";
export type SandboxNetworkAccess = "none" | "internet_client";

export interface SandboxFilesystemRule {
  path: string;
  access: SandboxFilesystemAccess;
}

export interface SandboxPolicy {
  filesystem: readonly SandboxFilesystemRule[];
  network: SandboxNetworkAccess;
}

export interface SandboxOsError {
  code: number;
  message: string;
}

export interface SandboxPathError {
  path: string;
  error: SandboxOsError;
}

export interface SandboxBrokerResult {
  started: boolean;
  timedOut: boolean;
  terminated: boolean;
  exitCode: number;
  osErrorBeforeTermination: SandboxOsError | null;
  finalError: SandboxOsError | null;
  registryFinalError: SandboxOsError | null;
  pathErrors: SandboxPathError[];
  stdout: string;
  stderr: string;
}

interface SandboxBrokerRequest {
  executable: string;
  args: unknown;
  cwd: string;
  stdin?: string;
  timeoutMs: number;
  refresh: boolean;
  sandbox: SandboxPolicy;
}

const REQUEST_MAGIC = Buffer.from("HHSBX003", "ascii");
const RESULT_MAGIC = Buffer.from("HHSBR001", "ascii");

function u32(value: number): Buffer {
  const output = Buffer.allocUnsafe(4);
  output.writeUInt32LE(value >>> 0, 0);
  return output;
}

function stringField(value: string): Buffer[] {
  const encoded = Buffer.from(value, "utf8");
  if (encoded.length > 0xffffffff) {
    throw new Error("Sandbox protocol string exceeds 4GiB.");
  }
  return [u32(encoded.length), encoded];
}

export function encodeSandboxRequest(request: SandboxBrokerRequest): Buffer {
  if (!Number.isInteger(request.timeoutMs) || request.timeoutMs <= 0) {
    throw new Error("Sandbox timeoutMs must be a positive integer.");
  }
  if (request.timeoutMs > 0xffffffff) {
    throw new Error("Sandbox timeoutMs exceeds protocol range.");
  }
  if (request.sandbox.filesystem.length > 0xffffffff) {
    throw new Error("Sandbox protocol item count exceeds uint32 range.");
  }

  const chunks: Buffer[] = [
    REQUEST_MAGIC,
    u32(request.timeoutMs),
    u32(request.sandbox.network === "internet_client" ? 1 : 0),
    u32(request.refresh ? 1 : 0),
    ...stringField(request.executable),
    ...stringField(request.cwd),
    ...stringField(JSON.stringify(request.args)),
  ];

  chunks.push(u32(request.sandbox.filesystem.length));
  for (const rule of request.sandbox.filesystem) {
    chunks.push(
      ...stringField(rule.path),
      u32(rule.access === "read_write" ? 1 : 0),
    );
  }

  chunks.push(...stringField(request.stdin ?? ""));
  return Buffer.concat(chunks);
}

class BufferReader {
  private offset = 0;

  constructor(private readonly buffer: Buffer) {}

  private require(bytes: number): void {
    if (bytes < 0 || this.offset + bytes > this.buffer.length) {
      throw new Error("Sandbox broker returned a truncated protocol response.");
    }
  }

  readMagic(expected: Buffer): void {
    this.require(expected.length);
    const actual = this.buffer.subarray(this.offset, this.offset + expected.length);
    this.offset += expected.length;
    if (!actual.equals(expected)) {
      throw new Error("Sandbox broker returned invalid protocol magic.");
    }
  }

  readU32(): number {
    this.require(4);
    const value = this.buffer.readUInt32LE(this.offset);
    this.offset += 4;
    return value;
  }

  readI32(): number {
    this.require(4);
    const value = this.buffer.readInt32LE(this.offset);
    this.offset += 4;
    return value;
  }

  readString(): string {
    const length = this.readU32();
    this.require(length);
    const value = this.buffer.toString("utf8", this.offset, this.offset + length);
    this.offset += length;
    return value;
  }

  readError(preserveZero = false): SandboxOsError | null {
    const code = this.readI32();
    const message = this.readString();
    return code === 0 && !preserveZero ? null : { code, message };
  }

  finish(): void {
    if (this.offset !== this.buffer.length) {
      throw new Error("Sandbox broker returned trailing protocol bytes.");
    }
  }
}

export function decodeSandboxResult(buffer: Buffer): SandboxBrokerResult {
  const reader = new BufferReader(buffer);
  reader.readMagic(RESULT_MAGIC);

  const started = reader.readU32() !== 0;
  const timedOut = reader.readU32() !== 0;
  const terminated = reader.readU32() !== 0;
  const exitCode = reader.readI32();
  const osErrorBeforeTermination = reader.readError(timedOut);
  const finalError = reader.readError();
  const registryFinalError = reader.readError();

  const pathErrorCount = reader.readU32();
  const pathErrors: SandboxPathError[] = [];
  for (let index = 0; index < pathErrorCount; index += 1) {
    const path = reader.readString();
    const error = reader.readError();
    if (error !== null) pathErrors.push({ path, error });
  }

  const stdout = reader.readString();
  const stderr = reader.readString();
  reader.finish();

  return {
    started,
    timedOut,
    terminated,
    exitCode,
    osErrorBeforeTermination,
    finalError,
    registryFinalError,
    pathErrors,
    stdout,
    stderr,
  };
}
