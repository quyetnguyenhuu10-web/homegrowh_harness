# BLOCK SESSION

## 1. Public API

`@hh/session` là public boundary duy nhất.

```ts
import * as session from "@hh/session";
```

Runtime chỉ có:

```ts
session.send(target, prompt, options?)
session.cancel(sessionId)
session.active()
session.subscribe(listener)
session.close()
```

Không có deep import public.

## 2. Send

```ts
const sessionId = await session.send(
  {
    scope: {
      kind: "repository",
      repositoryPath: "D:\\homegrowh_harness",
    },
    conversationId: "...",
  },
  "prompt",
);
```

Session tự lấy model đang chọn từ `@hh/provider`.

Caller không truyền:

```text
provider
model
apiKey
baseUrl
tool executor
context builder
database writer
compaction policy
```

## 3. Orchestration

Quy ước:

```text
Session = từ lúc user nhấn Send đến khi toàn bộ agent/tool loop kết thúc.
Request = đúng một lần gọi Provider.
```

Mọi Request thường, kể cả Request đầu Session và Request sau tool result, đều đi
chung đúng một preflight runtime. Không có pipeline riêng theo vị trí Request
trong Session.

Session trực tiếp phối hợp public API của:

```text
@hh/database
@hh/provider
@hh/compaction
@hh/tools
```

Flow của mọi Request theo đúng sơ đồ `docs/draw_architech/model_architech.png`:

```text
Session nhận currentRequest
    ↓
đọc một snapshot: requestsBeforeCurrent + requestsCurrent
    ↓
build Usage Context trong RAM
    ↓
tính token usage
    ↓
Compaction tự quyết định có cần nén hay không
    ↓
history sau compaction (nếu có)
    ↓
hard-limit check
    ↓
PASS
    ↓
commit pending compaction/currentRequest/contextUsage vào DB
    ↓
Provider.call
    ↓
persist stream / tool result
    ↓
nếu có Request tiếp theo → quay lại đúng pipeline trên
```

Trước khi `hard-limit check` PASS, preflight không được ghi row của Request hiện
tại, không được ghi compaction row và không được cập nhật `contextUsage`.

Scope context:

```text
Request thường
    = compaction boundary gần nhất + toàn bộ Session hiện tại đang diễn ra
    + system + tool definitions

Request compaction của Session N
    = history trước Session N, bắt đầu từ bản compaction mới nhất nếu có
    = không system
    = không tool definitions
    = không bất kỳ row nào của Session N
```

Nếu compaction chỉ phát sinh ở Request 2/3/... của Session N, Database vẫn đặt
row `compaction` theo thứ tự logic ngay trước row đầu tiên của Session N. `id`
row đã tồn tại không bị đổi; Database dùng thứ tự logic riêng để giữ boundary.

Preflight được chạy lại trước provider invocation sau tool result. Mỗi lần đều
hỏi Compaction qua public API; Session không tự quyết định ngưỡng hoặc bỏ qua
vì đã nén một lần. History trả về (kể cả khi không nén) được ghép với nguyên
requestsCurrent để gọi Provider. requestsCurrent chứa prompt hiện tại và mọi
request/tool result đã hoàn tất trong Session này.

Một hội thoại chỉ có một Session đang chạy. Hội thoại khác vẫn có thể chạy
độc lập. Cancel/close hủy cùng vòng đời, bao gồm request đặc biệt của Compaction.

Tool call được gom đủ stream, parse JSON rồi kiểm tra theo definition public
của tool (required, type, enum, properties, items, additionalProperties).
Call sai được thay bằng invalid_tool_call có ID duy nhất và arguments hợp lệ,
sau đó tạo tool result lỗi qua cùng đường thực thi; không gọi executor gốc.
Call hợp lệ giữ nguyên toàn bộ kết quả executor, kể cả lỗi nghiệp vụ.

Reply/reasoning được append ngay từng delta. Các đoạn khác loại không bị gom
ngược thứ tự. Tool call hoàn chỉnh và từng tool result được append tuần tự;
chỉ sau khi kết quả đã được ghi và thông báo mới bắt đầu request tiếp theo.

## 4. Database truth

Session không stream content trực tiếp cho UI.

Session notification chỉ báo trạng thái hoặc vị trí dữ liệu đã thay đổi:

```text
row
session
contextUsage
error
```

UI nhận `rowId` rồi đọc Database để render truth.

## 5. Private

Không public:

```text
context projection
system prompt
reasoning history selection
provider stream parser
tool-call parser
dispatcher
request state map
AbortController
eventIndex
tool execution routing
```

> Session chỉ sở hữu thứ tự, thời điểm và vòng đời phối hợp các block; không sở hữu implementation của các block đó.
