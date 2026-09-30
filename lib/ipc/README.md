# IPC errors

Every IPC result uses `std::optional<ipc::Error> error`. Success has `error == std::nullopt`.
On failure, `error` is authoritative; `value` is an empty owner and read `data` is empty.
A clean peer close at a frame boundary has `read_result.closed == true` and no error.

`ipc::Error` has exactly this JSON envelope:

```json
{
  "source": "ipc",
  "operation": "connect",
  "type": "system_error",
  "message": "<original OS message>",
  "data": [{"code": 2, "category": "generic", "api": "connect", "path": "<socket path>"}],
  "causes": []
}
```

`data` and `causes` are always arrays. `data` accepts arbitrary JSON values.
Native failures preserve the OS code, category, API and endpoint path in `data`.
Read/write failures also include the frame phase, expected size and byte progress.
Validation failures use `validation_error`; malformed or truncated frames use `protocol_error`.
Validation and EOF never invent native error codes. On Windows, a truncated-frame
error contains the original pipe failure in `causes` when an OS error was reported.
Independent setup and cleanup failures are kept as separate causes.

```cpp
auto connected = ipc::connect(endpoint_name);
if (connected.error)
{
    nlohmann::json event_data = {{"error", *connected.error}};
    // Pass event_data to the caller's event/lifecycle envelope.
}
else
{
    ipc::connection connection = std::move(connected.value);
}
```

The JSON serializer preserves all six fields and recursively preserves causes.
IPC framing carries bytes without interpreting error payloads, so JSON or CBOR
event packets retain the same error schema. An adapter adding no semantic context
forwards the original error; an adapter adding context places it in `causes`.
Event fields such as level, sequence, references and primary/secondary roles belong
to the event envelope.
