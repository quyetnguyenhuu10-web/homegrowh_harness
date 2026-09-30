import fs from "node:fs";
import path from "node:path";

const repo = path.resolve(import.meta.dirname, "..", "..");
const libRoot = path.join(repo, "lib");
const sourceExtensions = new Set([".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".js", ".jsx", ".mjs", ".cjs", ".ts", ".tsx"]);
const ignoredDirs = new Set(["node_modules", "build", "dist", "out", ".git"]);
const libs = fs.readdirSync(libRoot, { withFileTypes: true }).filter(e => e.isDirectory()).map(e => e.name).sort();
const libSet = new Set(libs);
const violations = [];
let filesChecked = 0;
let dependenciesChecked = 0;

function walk(dir, owner) {
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    if (ignoredDirs.has(entry.name)) continue;
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) walk(full, owner);
    else if (entry.isFile() && sourceExtensions.has(path.extname(entry.name).toLowerCase())) checkFile(full, owner);
  }
}

function dependencies(text) {
  const out = [];
  const patterns = [
    ["include", /^\s*#\s*include\s*[<\"]([^>\"]+)[>\"]/gm],
    ["import", /\b(?:import|export)\s+(?:[\s\S]*?\s+from\s+)?["']([^"']+)["']/g],
    ["require", /\brequire\s*\(\s*["']([^"']+)["']\s*\)/g],
    ["dynamic import", /\bimport\s*\(\s*["']([^"']+)["']\s*\)/g],
  ];
  for (const [kind, regex] of patterns) for (const m of text.matchAll(regex)) out.push({kind, specifier:m[1], index:m.index ?? 0});
  return out;
}

function targetLib(file, specifier) {
  const s = specifier.replaceAll("\\", "/");
  const explicit = s.match(/(?:^|\/)lib\/([^/]+)(?:\/|$)/);
  if (explicit && libSet.has(explicit[1])) return explicit[1];
  if (s.startsWith(".")) {
    const rel = path.relative(libRoot, path.resolve(path.dirname(file), specifier));
    if (!rel.startsWith("..") && !path.isAbsolute(rel)) {
      const first = rel.split(path.sep)[0];
      if (libSet.has(first)) return first;
    }
  }
  if (s.startsWith("hh/")) {
    const name = s.slice(3).split("/")[0];
    if (libSet.has(name)) return name;
  }
  // C/C++ include directories can hide the physical lib path, e.g. <provider/foo.h>.
  const first = s.split("/")[0];
  if (s.includes("/") && libSet.has(first)) return first;
  return null;
}

function checkFile(file, owner) {
  filesChecked++;
  const text = fs.readFileSync(file, "utf8");
  for (const dep of dependencies(text)) {
    dependenciesChecked++;
    const target = targetLib(file, dep.specifier);
    if (target && target !== owner) violations.push({owner,target,file:path.relative(repo,file).replaceAll("\\","/"),line:text.slice(0,dep.index).split(/\r?\n/).length,kind:dep.kind,specifier:dep.specifier});
  }
}

for (const lib of libs) walk(path.join(libRoot, lib), lib);
console.log(`lib boundary check: ${libs.length} libs, ${filesChecked} source files, ${dependenciesChecked} include/import references`);
if (!violations.length) { console.log("PASS: no cross-lib include/import dependencies found."); process.exit(0); }
console.error(`FAIL: ${violations.length} cross-lib boundary violation(s):`);
for (const v of violations) {
  console.error(`  ${v.file}:${v.line}`);
  console.error(`    ${v.owner} -> ${v.target}  [${v.kind}] ${JSON.stringify(v.specifier)}`);
}
process.exit(1);
