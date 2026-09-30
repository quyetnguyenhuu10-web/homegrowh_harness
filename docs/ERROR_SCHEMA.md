# Unified Error Schema

Toan bo repo dung **mot schema loi duy nhat**. Muc tieu giong `EventPort`: envelope co dinh, nhung payload mo de chap nhan moi loai loi tu Win32/errno den provider, sandbox, session, plugin va UI.

## Schema chot

TypeScript:

```ts
export type HHError = {
  source: string;
  operation: string;
  type: string;
  message: string;
  data: unknown[];
  causes: HHError[];
};
```

C++:

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

## Y nghia field

- `source`: module/domain sinh ra loi, vi du `provider`, `sandbox`, `sessions`, `plugin_loader`.
- `operation`: thao tac dang thuc hien khi loi xay ra, vi du `request`, `run_tool`, `grant_filesystem`, `invoke`.
- `type`: loai loi semantic de machine phan nhanh, vi du `system_error`, `http_error`, `protocol_error`, `dependency_error`.
- `message`: mo ta ngan gon cho nguoi doc.
- `data`: bat buoc la mang. Moi phan tu co the la bat ky JSON value nao; thong thuong dung object de chua nhom du lieu dac thu nhu OS code, path, API name, HTTP body, stack, plugin id, schema errors...
- `causes`: cac loi tang duoi gay ra loi hien tai. Luon la mang; khong dung `cause`, `inner_error`, `source_error`, `raw_error` rieng le. Ví dụ: Hàm 1 được hàm 2 dùng, hàm 2 được hàm 3 dùng. Thì hàm 2 sẽ chứa tối đa 1 phần tử, hàm 3 chứa tối đa 2 phần tử

## Nguyen tac bat buoc

1. Moi public/module error phai quy ve `HHError`/`Error`.
2. Khong tu tao schema loi rieng cho tung module.
3. Khong flatten loi thanh string neu van con thong tin co cau truc.
4. Tang tren khong duoc thay loi tang duoi bang mot loi ngheo thong tin hon.
5. Neu tang tren khong them semantic context, forward nguyen loi.
6. Chi wrap khi tang tren thuc su them y nghia. Loi tang duoi duoc dat trong `causes`.
7. `level`, `timestamp`, `sequence`, `references`, `primary/secondary` khong thuoc `HHError`; chung thuoc event/lifecycle envelope.
8. `code` khong nam top-level vi khong universal. Win32 `DWORD`, `errno`, HTTP status, plugin code... nam trong `data`.
9. `stack`, `path`, `errno`, `status_code`, `body`, `api`, `category`... deu nam trong `data`.
10. Neu co nhieu loi con doc lap, dung nhieu phan tu trong `causes` thay vi gop thanh mot chuoi.

## Vi du

### Win32 / system error

```json
{
  "source": "sandbox",
  "operation": "grant_filesystem",
  "type": "system_error",
  "message": "SetNamedSecurityInfoW failed",
  "data": [
    {
      "code": 5,
      "category": "system",
      "api": "SetNamedSecurityInfoW",
      "path": "D:\\tools"
    }
  ],
  "causes": []
}
```

### Provider HTTP

```json
{
  "source": "provider",
  "operation": "request",
  "type": "http_error",
  "message": "HTTP request failed with status 429",
  "data": [
    {
      "status_code": 429,
      "status_line": "HTTP/1.1 429 Too Many Requests",
      "reason": "Too Many Requests",
      "body": "..."
    }
  ],
  "causes": []
}
```

### Tang tren them context

```json
{
  "source": "sessions",
  "operation": "run_request",
  "type": "dependency_error",
  "message": "Provider request failed",
  "data": [],
  "causes": [
    {
      "source": "provider",
      "operation": "request",
      "type": "http_error",
      "message": "HTTP request failed with status 429",
      "data": [
        {
          "status_code": 429,
          "body": "..."
        }
      ],
      "causes": []
    }
  ]
}
```

## Result

TypeScript:

```ts
export type Result<T> =
  | { value: T; error: null }
  | { value: null; error: HHError };
```

C++ co the dung cung semantic: mot trong hai nhanh `value` hoac `error` la authoritative.

## EventPort

`EventPort` khong tao schema loi moi. Event chi boc `HHError` trong `data.error`:

```json
{
  "package": "sessions",
  "level": "error",
  "type": "command_failed",
  "references": [],
  "data": {
    "error": {
      "source": "sessions",
      "operation": "run_request",
      "type": "dependency_error",
      "message": "Provider request failed",
      "data": [],
      "causes": []
    }
  }
}
```

Primary/secondary la semantic cua event:

```text
command_failed     -> primary failure
secondary_error    -> secondary failure
```

Khong them `primary: true` hoac `secondary: true` vao `HHError`.

## Huong refactor

Duong loi mong muon sau khi chuan hoa:

```text
native/low-level failure
        |
        v
      HHError
        |
        +--> Result.error
        |
        +--> EventPort data.error
        |
        +--> IPC serialize nguyen schema
        |
        +--> plugin get_error / stream_error
        |
        `--> UI
```

Moi adapter chi duoc bo sung context hoac serialize/deserialize schema. Khong duoc lam mat identity, code, path, operation hay payload cua loi tang duoi.
