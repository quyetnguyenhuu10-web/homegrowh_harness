# Provider errors

`<provider>` exports one error envelope:

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

`request`, `compaction`, `provider_from_name`, `usage_from_event`,
`parse_usage`, `Stream::write`, and the error JSON codecs return `Result<T>`.
A returned result contains exactly one engaged branch: `value` or `error`.
`Result<void>` uses an engaged `std::monostate` as its successful value.

```cpp
auto result = provider::request(selected, url, api_key, body);
if (result.error)
{
    auto encoded = provider::serialize_error(*result.error);
    if (encoded.value)
        consume_error_json(*encoded.value);
}
else
{
    consume_usage(*result.value);
}
```

Event and completion callbacks return `Result<void>`. Return
`Result<void>::success()` after handling an event, or
`Result<void>::failure(std::move(error))` to stop the request. The request
forwards callback errors without changing their source, operation, payload,
or causes. Native exceptions from third-party callbacks are normalized at
the callback boundary. Completion runs only after a successful request and
successful usage parsing.

HTTP failures retain status, status line, reason, response body, and URL in
`data`. Transport failures retain the original CURLcode, CPR code and message,
and OS error code when available. JSON exceptions retain their original
message, exception id, parse byte when available, and input payload.
Non-JSON SSE events other than the protocol's `[DONE]` marker return a
`protocol_error`. Provider error events retain their complete native payload;
already normalized error events are forwarded unchanged.

Compaction adds its operation and request phase when a dependency fails.
The original error is an immediate member of `causes`; its existing nested
causes remain intact. Unavailable usage is a successful request result,
while compaction requires usage and a nonempty summary for its summary step.

`serialize_error` and `deserialize_error` preserve all six fields recursively,
including arbitrary JSON values in `data`. Deserialization rejects missing
fields, extra envelope fields, non-array `data` or `causes`, and invalid child
errors. Metadata such as codes, paths, stack frames and HTTP fields belongs
inside `data`; event severity and lifecycle metadata stay outside the error.

`Stream::write(output)` returns a result and retains failures in `error()`.
`Stream::usage()` returns a result borrowing the completed usage through
`std::reference_wrapper`; its lifetime follows the stream. Stream construction
failures are retained in `error()` and returned by `write()` and `usage()`.
The insertion operator also retains errors and sets the output failure state.

## Compatibility

The previous `HttpError` exception API is removed. Request and compaction
callers inspect `Result` instead of catching exceptions. Existing event and
completion callbacks must return `Result<void>`. Usage helpers also return
results, and stream usage is accessed through `result.value->get()`.

Stream usage request options are declared in `src/request/provider_types.json`
and emitted by the code generator. Runtime request preparation does not
special-case provider names.

## Tests

Configure the repository with `HH_BUILD_TESTS=ON` or `PROVIDER_BUILD_TESTS=ON`,
then build only the provider targets:

```text
cmake --build build --config Release --target provider_error_schema_test provider_call_llm
ctest --test-dir build/lib/provider -C Release --output-on-failure
```

The regression test runs against an ephemeral local HTTP/SSE server and needs
no provider credentials or external service. It covers error JSON round trips,
native errors, callback forwarding, compaction causes, and stream failures.
`provider_call_llm` is a configurable manual example, not an automated live call:

```text
provider_call_llm <provider> <url> <model> <prompt> [tools.json]
```

The example reads its API key from `HH_API_KEY`.
