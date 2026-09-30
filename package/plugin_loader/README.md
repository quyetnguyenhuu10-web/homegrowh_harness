# @hh/plugin-loader

Bộ nạp dùng chung đặt tại `package/plugin_loader/`. Các plugin cụ thể
tiếp tục nằm trong `plugins/`, ví dụ `plugins/session-api/`.

Thư viện dùng cấu trúc của `lib/ipc-client`: `src/index.ts` là entry
công khai, mã chi tiết nằm trong `src/`, test nằm trong `tests/`,
TypeScript build từ `src/` sang `dist/`.

Host, plugin và test import qua tên package, giống public API của tools:

```ts
import * as plugin_loader from "@hh/plugin-loader";
import * as ipc_client from "@hh/ipc-client";
```

Package exports trỏ tới entry đã build; mã gọi chỉ dùng tên public:

```ts
import { PluginRegistry, get_error } from "@hh/plugin-loader";
```

Cài và build từ thư mục gốc repo:

```powershell
npm --prefix package/plugin_loader ci
npm --prefix lib/ipc-client ci
npm --prefix package/plugin_loader run build
npm --prefix lib/ipc-client run build
npm install
npm --prefix plugins/session-api run build
```

`src/index.ts` là facade công khai. Chi tiết nằm trong `src/loader/`: manifest,
schema, module, invoke, handles, registry và types. Kiểu kết quả/lỗi
dùng chung nằm trong `src/result.ts`; adapter exception nằm trong `src/error.ts`
và phần chuyển payload sang JSON nằm trong `src/error/`.

## Schema lỗi duy nhất

Mọi lỗi công khai của loader và error queue dùng cùng envelope:

```ts
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

`PluginLoaderError` là alias của `HHError`, `PluginResult<T>` là alias của
`Result<T>`. `data` chứa payload JSON mở: `code`, `errno`, `syscall`, `path`,
`stack`, `manifestPath`, `pluginId`, `apiId`, `direction`, `schema_errors`...
Các field này không nằm ở top-level. `causes` luôn là mảng lỗi cùng schema.
Thông tin level, timestamp, sequence và primary/secondary thuộc event.

`normalize_error(value, { source, operation, data? })` chuyển exception tại
biên native thành `HHError`. Adapter giữ message, stack và các thuộc tính gốc;
`Error.cause` và từng lỗi trong `AggregateError.errors` đi vào `causes`.
Giá trị ngoài JSON như `undefined`, bigint và tham chiếu vòng được biểu diễn
bằng dữ liệu có type hoặc `$ref`, để serialize không mất thông tin.
Lỗi đã là `HHError` được trả nguyên tham chiếu. Loader chỉ wrap lỗi đó khi
thêm ngữ cảnh manifest/plugin/API; lỗi gốc nằm nguyên trong `causes`.

## API lấy lỗi

Hai cách truy cập cùng schema lỗi:

- `get_error({ result })` export trực tiếp của loader đọc
  `PluginResult.error` của thao tác loader cụ thể.
- Mọi plugin bắt buộc phải khai báo API `get_error()` chuẩn để host đọc
  error queue do chính plugin sở hữu.

Contract plugin `get_error()` không nhận tham số và trả `HHError[]` theo
FIFO. Mỗi lần gọi sẽ drain queue. `PluginErrorQueue.push()` trả lỗi đã chuẩn hóa;
plugin có thể throw chính lỗi đó để queue và loader cùng giữ identity của lỗi.
Loader cũng chuẩn hóa các entry thô từ plugin cũ trước khi validate output
của `get_error`. Output schema khai báo `type: "array"`, có thể ràng buộc
entry bằng schema `HHError`; `items: true` vẫn được chấp nhận cho tương thích.
Manifest của adapter loader ràng buộc từng entry theo `HHError`.

```ts
import { get_error, PluginRegistry } from "@hh/plugin-loader";

const registry = new PluginRegistry();
const result = await registry.load("plugins/example/plugin.json");
const error = get_error({ result });
if (error !== null) {
    console.error(JSON.stringify(error));
}
```

`package/plugin_loader/plugin.json` cũng tuân theo contract plugin chung:
`id: 1`, `name: "get_error"`, input rỗng, output `HHError[]`.
`src/api.ts` giữ queue riêng cho adapter này; manifest trỏ tới `dist/api.js`.
Host có thể gọi qua handle:

```ts
const loaded = await registry.load("package/plugin_loader/plugin.json");
if (loaded.error === null) {
    const retrieved = await loaded.value.call("get_error");
    if (retrieved.error === null) {
        for (const error of retrieved.value) {
            console.log(error);
        }
    }
}
```

Loader kiểm tra contract này ngay khi load manifest. Plugin thiếu `get_error`
hoặc khai báo sai input/output sẽ fail ở `manifest_validate`.

```ts
import { PluginErrorQueue } from "@hh/plugin-loader";

const errors = new PluginErrorQueue({ source: "example_plugin", operation: "invoke" });
// Trong catch của plugin:
// throw errors.push(error);
// Trong get_error(): return errors.drain();
```

## Nạp và gọi plugin

`load()` và API handle `invoke()` trả `{ value, error }`.
Thành công: `error === null`. Thất bại: `value === null`.
Kiểm tra `error !== null` để TypeScript thu hẹp kiểu kết quả.

Host có thể gọi API tổng quát bằng positional arguments qua
`PluginHandle.call(name, ...args)`. Với đường gọi này, thứ tự trong
`input.required[]` của `plugin.json` chính là thứ tự tham số. Loader tự
ánh xạ `args[i]` vào `input[required[i]]`, sau đó vẫn chạy validation input,
invoke plugin và validation output như `PluginApiHandle.invoke()`.

```ts
const result = await loaded.value.call("run_request", 1n);
// input gửi vào plugin: { command_id: 1n }

const registered = await loaded.value.call(
    "register_session",
    1n,
    config,
);
// input gửi vào plugin: { command_id: 1n, config }
```

Nếu API không có `input.required[]`, tên API không tồn tại hoặc số tham số
không khớp, `call()` trả lỗi `api_resolve` / `argument_map`; loader không
đoán tên hay thứ tự tham số.

```ts
import { PluginRegistry } from "@hh/plugin-loader";

const registry = new PluginRegistry();
const loaded = await registry.load("plugins/example/plugin.json");
if (loaded.error !== null) {
    const error = loaded.error;
    console.error(JSON.stringify(error));
} else {
    const api = loaded.value.apis.find((api) => api.name === "run");
    if (api) {
        const result = await api.invoke({ message: "hello" });
        if (result.error !== null) {
            const error = result.error;
            console.error(JSON.stringify(error));
        } else {
            console.log(result.value);
        }
    }
}
```

Lỗi AJV giữ đầy đủ mảng `schema_errors` trong `data`, gồm keyword, params,
instancePath và schemaPath. Mỗi kết quả giữ snapshot riêng của mảng này.
Vi phạm hợp đồng có `code` và dữ liệu liên quan trong `data`; exception OS
giữ code, errno, syscall và path gốc. Mỗi kết quả giữ lỗi riêng; không có
trạng thái lastError chung.

Các output thành công, gồm `stream_error` trả `HHError`, `Result` hoặc event,
được chuyển nguyên trạng. EventPort tiếp tục giữ lỗi tại `data.error`; loader
không thêm field lifecycle vào `HHError` hay làm phẳng payload của event.

`loadAll(root)` trả `{ plugins, error }`, dừng tại lỗi đầu tiên và giữ
những plugin đã đăng ký trước đó trong cả kết quả và registry.
Thư mục không có `plugin.json` được bỏ qua; lỗi đọc khác được trả về.
`list()` tiếp tục trả danh sách handle đã đăng ký.

Manifest được kiểm tra theo `src/plugin.schema.json`; ID API phải là số
nguyên an toàn, ID/tên API phải duy nhất. Input/output tiếp tục được
kiểm tra bằng JSON Schema và `x-hh-type`.

Kết quả của loader bọc bên ngoài output của plugin. Interface
`Plugin.invoke(request)` trong module plugin vẫn trả output của chính
plugin; chỉ `PluginApiHandle.invoke(input)` trả `PluginResult<unknown>`.
