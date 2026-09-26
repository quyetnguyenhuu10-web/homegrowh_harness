import { createReadStream } from "node:fs";
import {
  access,
  open,
  readFile,
  readdir,
  stat,
} from "node:fs/promises";
import { extname } from "node:path";
import { createInterface } from "node:readline";

export const DEFAULT_READ_LIMIT = 2_000;
export const MAX_LINE_LENGTH = 2_000;
export const MAX_BYTES = 50 * 1_024;
export const SAMPLE_BYTES = 4_096;

const BINARY_EXTENSIONS = new Set([
  ".zip", ".tar", ".gz", ".exe", ".dll", ".so", ".class", ".jar", ".war",
  ".7z", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".odt", ".ods",
  ".odp", ".bin", ".dat", ".obj", ".o", ".a", ".lib", ".wasm", ".pyc", ".pyo",
]);

export async function exists(path: string): Promise<boolean> {
  try {
    await access(path);
    return true;
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") return false;
    throw error;
  }
}

export async function readSample(
  path: string,
  size: number,
  sampleSize = SAMPLE_BYTES,
): Promise<Buffer> {
  if (size === 0) return Buffer.alloc(0);
  const handle = await open(path, "r");
  try {
    const buffer = Buffer.alloc(Math.min(sampleSize, size));
    const read = await handle.read(buffer, 0, buffer.length, 0);
    return buffer.subarray(0, read.bytesRead);
  } finally {
    await handle.close();
  }
}

export function isBinaryFile(path: string, bytes: Uint8Array): boolean {
  if (BINARY_EXTENSIONS.has(extname(path).toLowerCase())) return true;
  if (bytes.length === 0) return false;
  let nonPrintable = 0;
  for (const byte of bytes) {
    if (byte === 0) return true;
    if (byte < 9 || (byte > 13 && byte < 32)) nonPrintable += 1;
  }
  return nonPrintable / bytes.length > 0.3;
}

export async function readLines(
  path: string,
  offset: number,
  limit: number,
): Promise<{
  raw: string[];
  count: number;
  cut: boolean;
  more: boolean;
  offset: number;
}> {
  const start = offset - 1;
  const raw: string[] = [];
  let bytes = 0;
  let count = 0;
  let cut = false;
  let more = false;
  const stream = createReadStream(path, { encoding: "utf8" });
  const lines = createInterface({ input: stream, crlfDelay: Infinity });

  for await (const text of lines) {
    count += 1;
    if (count <= start) continue;
    if (raw.length >= limit) {
      more = true;
      continue;
    }

    const line =
      text.length > MAX_LINE_LENGTH
        ? `${text.slice(0, MAX_LINE_LENGTH)}... (line truncated to ${MAX_LINE_LENGTH} chars)`
        : text;
    const size = Buffer.byteLength(line, "utf8") + (raw.length > 0 ? 1 : 0);
    if (bytes + size <= MAX_BYTES) {
      raw.push(line);
      bytes += size;
      continue;
    }

    cut = true;
    more = true;
    stream.destroy();
    break;
  }

  return { raw, count, cut, more, offset };
}

export async function readDirectoryNames(path: string): Promise<string[]> {
  const entries = await readdir(path, { withFileTypes: true });
  return entries
    .map((entry) => (entry.isDirectory() ? `${entry.name}/` : entry.name))
    .sort((left, right) => left.localeCompare(right));
}

export async function readFileWithBom(path: string): Promise<{
  bom: boolean;
  text: string;
}> {
  const buffer = await readFile(path);
  const bom =
    buffer.length >= 3 &&
    buffer[0] === 0xef &&
    buffer[1] === 0xbb &&
    buffer[2] === 0xbf;
  return {
    bom,
    text: buffer.subarray(bom ? 3 : 0).toString("utf8"),
  };
}

export function splitBom(text: string): { bom: boolean; text: string } {
  return text.charCodeAt(0) === 0xfeff
    ? { bom: true, text: text.slice(1) }
    : { bom: false, text };
}

export function joinBom(text: string, bom: boolean): string {
  return bom ? `\ufeff${text}` : text;
}

export function normalizeLineEndings(text: string): string {
  return text.replaceAll("\r\n", "\n");
}

export function detectLineEnding(text: string): "\n" | "\r\n" {
  return text.includes("\r\n") ? "\r\n" : "\n";
}

export function convertToLineEnding(
  text: string,
  ending: "\n" | "\r\n",
): string {
  return ending === "\n" ? text : text.replaceAll("\n", "\r\n");
}
