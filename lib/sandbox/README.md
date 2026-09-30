# Sandbox errors

The public entry point is `#include <sandbox>`.

All failures use `sandbox::Error` with exactly six fields:
`source`, `operation`, `type`, `message`, `data`, and `causes`.
`data` and `causes` are always arrays. Native codes, API names, paths,
HRESULTs, exception details, and malformed registry contents are retained in
`data`. Independent failures are separate entries in `causes`.
Malformed registry contents use `body_bytes` so invalid UTF-8 bytes survive
JSON transport.

`Result<T>` contains an optional value and an optional error. Its factories
populate exactly one branch. `Result<void>` uses a present `std::monostate`
for success. Values and errors transfer ownership through the factories.

`serialize_error` and `deserialize_error` return `Result` and preserve all
fields recursively, including errors from another source. Deserialization
validates the six-field shape and retains the complete input when validation
fails. The existing JSON ADL serializer also emits the same shape.

## Configuration

`config::add(const config_option&)` returns `Result<void>`. A failed addition
is retained in `config::error()`; subsequent additions forward that error.
The initializer-list constructor retains its first error. A process with an
invalid configuration returns that original error before granting permissions
or creating a child.

## Processes

`process(process_request&)` returns `Result<void>` and fills
`request.results`, including partial stdout/stderr and lifecycle state.
A single failure is forwarded unchanged. Multiple independent failures are
collected in `causes`. A deadline produces a semantic `timeout` error with
the configured duration and any observed failures as causes.

Process exit status, start/termination flags, and per-path configuration
results remain in the lifecycle packet. A completed child with a nonzero
exit code still reports successful OS execution; callers interpret
`request.results.state.exit_code` for their own command semantics.

Windows failures retain unsigned DWORD values and original HRESULTs.
Filesystem exceptions retain both native paths and their original message.
Linux child-setup packets transport native facts to the parent, where they
are converted to the same `Error` schema. Output-worker exceptions are
captured and returned without escaping the worker thread.

## Validation

Configure from the repository root with `HH_BUILD_TESTS=ON`, then run:

```powershell
cmake --build build --config Release --target sandbox_error_schema_test sandbox_registry_test sandbox_process_test --parallel 4
ctest --test-dir build/lib/sandbox -C Release --output-on-failure
```

Windows process integration tests require permission to create an
AppContainer profile. Fixtures use dedicated temporary directories and a
registry-state override.
