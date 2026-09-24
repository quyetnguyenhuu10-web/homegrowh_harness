import assert from "node:assert/strict";

const publicImport = "@hh/tools";
const tools = await import(publicImport);

assert.deepEqual(
  Object.keys(tools).sort(),
  [
    "edit_file",
    "glob",
    "grep",
    "read",
    "todowrite",
    "webfetch",
    "write",
  ],
);

assert.equal(tools.read.definition.function.name, "read");
assert.equal(tools.write.definition.function.name, "write");
assert.equal(tools.edit_file.definition.function.name, "edit_file");

const privateImport = "@hh/tools/registry";

await assert.rejects(
  import(privateImport),
  (error: unknown) =>
    error instanceof Error &&
    "code" in error &&
    error.code === "ERR_PACKAGE_PATH_NOT_EXPORTED",
);
