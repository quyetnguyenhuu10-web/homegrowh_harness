import { chmodSync, copyFileSync, existsSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const repositoryRoot = resolve(scriptDirectory, "..", "..", "..");
const executableDirectory = resolve(repositoryRoot, "executable");
const target = resolve(
  executableDirectory,
  process.platform === "win32" ? "node.exe" : "node",
);

if (existsSync(target)) {
  console.log(`Node runtime already exists: ${target}`);
  process.exit(0);
}

mkdirSync(executableDirectory, { recursive: true });
copyFileSync(process.execPath, target);

if (process.platform !== "win32") {
  chmodSync(target, 0o755);
}

console.log(`Installed Node runtime: ${target}`);
