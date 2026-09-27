import { readFile, readdir, stat } from "node:fs/promises";
import { extname, join, relative } from "node:path";

const IGNORED_WALK_DIRS = new Set([
  ".git",
  "node_modules",
  "dist",
  "build",
  ".next",
  ".vite",
  ".turbo",
  ".codebase-memory",
]);

const BINARY_EXTENSIONS = new Set([
  ".zip", ".tar", ".gz", ".exe", ".dll", ".so", ".class", ".jar", ".war",
  ".7z", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".odt", ".ods",
  ".odp", ".bin", ".dat", ".obj", ".o", ".a", ".lib", ".wasm", ".pyc", ".pyo",
]);

export interface WalkedFile {
  path: string;
  relativePath: string;
  mtimeMs: number;
}

export async function walkFiles(root: string): Promise<WalkedFile[]> {
  const output: WalkedFile[] = [];
  await walk(root, root, output);
  return output;
}

async function walk(root: string, current: string, output: WalkedFile[]): Promise<void> {
  const entries = await readdir(current, { withFileTypes: true }).catch(() => []);
  for (const entry of entries) {
    const full = join(current, entry.name);
    if (entry.isDirectory()) {
      if (!IGNORED_WALK_DIRS.has(entry.name)) await walk(root, full, output);
      continue;
    }
    if (!entry.isFile()) continue;
    const info = await stat(full).catch(() => null);
    if (!info) continue;
    output.push({
      path: full,
      relativePath: normalizeSlashes(relative(root, full)),
      mtimeMs: info.mtimeMs,
    });
  }
}

export function createGlobMatcher(pattern: string): (value: string) => boolean {
  const regex = new RegExp(globToRegexSource(normalizeSlashes(pattern)));
  return (value) => regex.test(normalizeSlashes(value));
}

function globToRegexSource(pattern: string): string {
  let output = "^";
  for (let index = 0; index < pattern.length; index += 1) {
    const char = pattern[index];
    const next = pattern[index + 1];
    if (char === "*") {
      if (next === "*") {
        if (pattern[index + 2] === "/") {
          output += "(?:.*/)?";
          index += 2;
        } else {
          output += ".*";
          index += 1;
        }
      } else {
        output += "[^/]*";
      }
      continue;
    }
    if (char === "?") {
      output += "[^/]";
      continue;
    }
    if (char === "{") {
      const end = pattern.indexOf("}", index + 1);
      if (end !== -1) {
        const body = pattern
          .slice(index + 1, end)
          .split(",")
          .map(escapeRegex)
          .join("|");
        output += `(?:${body})`;
        index = end;
        continue;
      }
    }
    output += escapeRegex(char);
  }
  return `${output}$`;
}

function normalizeSlashes(value: string): string {
  return value.replace(/\\/g, "/");
}

function escapeRegex(value: string): string {
  return value.replace(/[|\\{}()[\]^$+*?.]/g, "\\$&");
}

export async function readTextFileIfText(path: string): Promise<string | null> {
  if (BINARY_EXTENSIONS.has(extname(path).toLowerCase())) return null;
  const buffer = await readFile(path).catch(() => null);
  if (!buffer) return null;
  const sample = buffer.subarray(0, Math.min(buffer.length, 4096));
  if (sample.includes(0)) return null;
  return buffer.toString("utf8");
}
