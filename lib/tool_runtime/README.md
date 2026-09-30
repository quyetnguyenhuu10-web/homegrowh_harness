# tool_runtime errors

The sole error envelope is `tool_runtime::Error`, declared in the umbrella header
`include/tool_runtime/tool_runtime.h`:

```cpp
struct Error
{
    std::string source;
    std::string operation;
    std::string type;
    std::string message;
    nlohmann::json::array_t data;
    std::vector<Error> causes;
};
```

`data` and `causes` always serialize as arrays, including when empty. There are
exactly six top-level fields. Native codes, API names, paths, JSON parser ids and
byte positions, exception types, stderr, and process exit status belong in `data`.
Invalid UTF-8 input/output is preserved as an object containing `encoding: "bytes"`
and an array of byte values, so reporting an encoding failure does not corrupt the
original bytes or fail JSON serialization.

`deserialize_error` returns `Result<Error>` and validates the entire recursive
envelope. `Result<T>` returns either an owned value or an owned `Error`. Success
has no error; failure has no value. The serialized tool result items use
`{"ok": true, "value": ..., "error": null}` or
`{"ok": false, "value": null, "error": ...}`. `ok` belongs to the tool envelope.
The previous result-item `result` field is now `value`; successful worker payloads
inside that value retain their original structure.

A conforming worker error is forwarded unchanged unless the adapter has context
to add. Legacy worker fields are retained in `data`; legacy cause aliases are
converted into recursive `causes`. Separate failed result entries remain separate
causes even when their content is identical. A process failure or unsuccessful
exit adds execution context and retains reported worker errors in `causes`.
Batch failures retain partial success results, their positions, and envelope
metadata in `data`.
There are no event lifecycle fields or primary/secondary flags in `Error`.

Process handles and pipe descriptors use RAII. Reads, writes, waits, and cleanup
retain their original native errors. On Linux, a close-on-exec startup pipe carries
the actual `dup2`, `close`, `chdir`, or `execv` errno back to the parent; exit codes
126/127 alone are not treated as the original error. SIGPIPE is blocked only in
the stdin writer thread, with its original signal mask restored afterward.

## Isolated build and tests

Configure this directory directly with an existing nlohmann_json CMake package:

```text
cmake -S lib/tool_runtime -B lib/tool_runtime/build -Dnlohmann_json_DIR=<package directory> -DTOOL_RUNTIME_PACKAGE_WORKERS=OFF -DTOOL_RUNTIME_BUILD_TESTS=ON
cmake --build lib/tool_runtime/build --config Release
ctest --test-dir lib/tool_runtime/build -C Release --output-on-failure
```

This builds and tests only this library and its fixtures. When configured by the
parent project with `edit_file` available, worker packaging keeps its existing
default behavior. The C++ regression suite checks schema round trips, legacy
adapters, native failure payloads, concurrent pipe I/O, quoting, and resource
cleanup. The Python suite checks the executable's complete JSON protocol through
native and TypeScript worker fixtures; TypeScript checks require an existing Node
installation.
