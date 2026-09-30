# IPC client

All error-producing operations return `Result<T>` using one error schema:

```typescript
export type HHError = {
    source: string;
    operation: string;
    type: string;
    message: string;
    data: unknown[];
    causes: HHError[];
};

export type Result<T> =
    | { value: T; error: null }
    | { value: null; error: HHError };
```

Import both types from `@hh/ipc-client`. Inspect `error` to select the authoritative
branch; `null` is also a successful value at the end of a receive stream.

```typescript
import { connect } from "@hh/ipc-client";

const connected = await connect(endpointName);
if (connected.error !== null) {
    displayError(connected.error);
} else {
    const connection = connected.value;
    const sent = await connection.send(payload);
    if (sent.error !== null) {
        displayError(sent.error);
    }
    const cleanup = connection.destroyTransport();
    if (cleanup.error !== null) {
        displayError(cleanup.error);
    }
}
```

| API | Return type |
| --- | --- |
| `connect(name)` | `Promise<Result<IpcConnection>>` |
| `listen(name)` | `Promise<Result<IpcServer>>` |
| `IpcServer.accept()` | `Promise<Result<IpcConnection>>` |
| `IpcServer.close()` | `Promise<Result<void>>` |
| `IpcConnection.send(value)` | `Promise<Result<void>>` |
| `IpcConnection.receive()` | `Promise<Result<unknown \| null>>` |
| `IpcConnection.messages()` | `AsyncGenerator<Result<unknown>, void, void>` |
| `IpcConnection.closeTransport()` | `Result<void>` |
| `IpcConnection.destroyTransport()` | `Result<void>` |

`messages()` yields successful values wrapped in `Result`, yields a failure once
and stops, and finishes at clean EOF. As in the original API, a received CBOR
`null` also ends `messages()`.

```typescript
for await (const message of connection.messages()) {
    if (message.error !== null) {
        return message;
    }
    consume(message.value);
}
```

`closeTransport()` requests a graceful socket end; `destroyTransport()` requests
immediate destruction. Their results describe the synchronous request. Later
socket error events remain available to `send()`/`receive()`. The server owns
unaccepted connections and destroys them when closing. Callers own accepted
connections and must close or destroy them before awaiting server shutdown.

Native failures retain their original message and own properties, including
`code`, `errno`, `syscall`, `path`, `stack`, and structured provider details in
`data`. Native `cause` and `AggregateError.errors` are converted to `causes`.
Existing `HHError` values pass through by reference with their original source,
operation, payload, and causes. The client layer forwards lower errors without
adding a wrapper. Multiple independent setup or cleanup failures use distinct
entries in `causes`.

CBOR transports `HHError` as ordinary payload without translating its schema.
An event can carry the same value in `data.error`; level, references, timestamp,
sequence, and primary/secondary semantics belong to the event envelope.

The 4-byte little-endian frame header and existing size limit are unchanged.
These Result return types replace the previous thrown/rejected errors and raw
success values.

Run `npm run typecheck` and `npm test` from this directory. Tests cover native
failures, cause chains, framing, codec errors, resource cleanup, and real IPC.
