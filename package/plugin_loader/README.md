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
dùng chung nằm trong `src/result.ts`.

## API lấy lỗi

`get_error({ result })` trả `PluginLoaderError | null`: lỗi của đúng lần
gọi đó hoặc null nếu thành công. Dùng được với kết quả `load()`,
`loadAll()` và API handle `invoke()`. API trả cùng tham chiếu lỗi,
không copy, không xóa và không ghi đè lỗi của lần gọi khác.

```ts
import { get_error, PluginRegistry } from "@hh/plugin-loader";

const registry = new PluginRegistry();
const result = await registry.load("plugins/example/plugin.json");
const error = get_error({ result });
if (error !== null) {
    console.error(error.operation, error.cause);
}
```

`package/plugin_loader/plugin.json` khai báo API `id: 1`, `name: "get_error"`,
input `{ result: { error, ... } }`, output `PluginLoaderError | null`.
`src/api.ts` là adapter riêng dispatch API qua chuẩn `plugin.invoke(request)`,
giống `session-api`; manifest trỏ tới `dist/api.js`. Entry thư viện
`dist/index.js` chỉ cung cấp API import trực tiếp.
Host có thể đọc manifest để tìm và gọi adapter:

```ts
const loaded = await registry.load("package/plugin_loader/plugin.json");
if (loaded.error === null) {
    const api = loaded.value.apis.find((api) => api.name === "get_error");
    if (api) {
        const retrieved = await api.invoke({ result });
        if (retrieved.error === null) {
            const originalError = retrieved.value;
            console.log(originalError);
        }
    }
}
```

Khi gọi qua handle, `retrieved.error` là lỗi của thao tác lấy lỗi;
`retrieved.value` là lỗi gốc cần đọc. `cause` giữ giá trị JavaScript gốc,
kể cả Error, bigint hoặc undefined; API dùng trong cùng tiến trình.

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
    console.error(error.operation, error.manifestPath, error.cause);
} else {
    const api = loaded.value.apis.find((api) => api.name === "run");
    if (api) {
        const result = await api.invoke({ message: "hello" });
        if (result.error !== null) {
            const error = result.error;
            console.error(error.pluginId, error.apiId, error.operation, error.cause);
        } else {
            console.log(result.value);
        }
    }
}
```

`error.cause` giữ nguyên exception được catch (kể cả giá trị không phải
Error), hoặc mảng lỗi gốc của AJV. Các vi phạm hợp đồng do loader kiểm
tra trả dữ liệu `{ code, ... }`, không ném Error hay suy diễn thông báo
thay lỗi gốc. Mỗi kết quả giữ lỗi riêng; không có trạng thái lastError chung.

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
