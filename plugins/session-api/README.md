# Session API

This plugin uses the shared `HHError` and `Result<T>` types from
`@hh/ipc-client`. Native process failures are adapted into the same six-field
envelope: `source`, `operation`, `type`, `message`, `data`, `causes`.
Existing HHError values pass through by reference with their original source,
operation, payload, and cause tree.

Native messages, codes, errno, syscall, executable path, spawn arguments, stack,
and additional structured properties remain in `data`. Native `cause` and
`AggregateError.errors` become `causes`. Multiple independent startup or shutdown
failures have distinct causes; successful cleanup preserves the original failure.

All plugin APIs except `get_error` return:

```typescript
type Result<T> =
    | { value: T; error: null }
    | { value: null; error: HHError };
```

| API                                                                                                                           | Success value                                                 |
| ----------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| `register_session`, `declare_request`, `run_request`, `declare_response`, `run_response`, `declare_tool`, `run_tool`, `close` | `{ command_id, state, result }`                               |
| `open_runtime`                                                                                                                | `{ runtime_id }`                                              |
| `send`                                                                                                                        | `null`                                                        |
| `receive`                                                                                                                     | Parsed RuntimeEvent, or `null` at EOF                         |
| `close_runtime`                                                                                                               | `{ code, signal }` describing the observed process exit       |
| `parse_event`                                                                                                                 | Parsed RuntimeEvent                                           |
| `stream_event`                                                                                                                | Original seven-element EventPort wire event, or `null` at EOF |
| `stream_error`                                                                                                                | Original HHError from an error event, or `null` at EOF        |
| `get_error`                                                                                                                   | A direct `HHError[]` array of queued API failures             |

`get_error` retains the direct array return contract required by the current
plugin loader. Its schema additionally validates each entry as HHError, and
draining the queue returns the same objects reported by failed invocations.
The other output schemas in `plugin.json` validate both Result branches.
The existing loader adds its own result envelope around an invocation; its
successful `value` contains the plugin result, or the direct `get_error` array.

The direct runtime functions and command builders in `src/` also return Result.
Inspect `error` before reading `value`; a successful stream EOF has both
`value: null` and `error: null`.

EventPort error payloads must contain a valid HHError in `data.error`. Parsed
events retain that payload and keep level, sequence, timestamp, references, and
primary/secondary event semantics in the event envelope. Provider status, body,
path, code, exception, and stack are read from `data.error.data` and its causes.
These fields are no longer duplicated on parsed RuntimeEvent objects.
Legacy error payloads that lack HHError produce a structured protocol error
containing the original packet and the validation location.

Event and error streams are independent. Reading `stream_error` leaves the
original event available to `stream_event`/`receive`, including its lifecycle
metadata. Terminal command failures fail their corresponding command waiter
with the original HHError. Fatal runtime/protocol events fail all command
waiters; parser, IPC, and native process failures also finish pending stream
consumers with Result errors. Late process error listeners remain active until
the process closes.

Shutdown gathers transport, process, reader, and listener failures. If the OS
refuses destruction or process termination, the failed close returns promptly
and retains the runtime entry so cleanup can be retried. All successfully
released runtimes are removed from the registry.

Run `npm run typecheck` and `npm test` in this directory. Tests include validation,
cause preservation, process and stream lifecycle failures, the existing plugin
loader's schema checks, and a real Node child process communicating over IPC.
