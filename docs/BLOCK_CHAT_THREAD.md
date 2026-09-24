# BLOCK CHAT THREAD

## 1. Vai trò

`@hh/chat-thread` là UI block.

ChatThread không điều phối model/tool/compaction và không sở hữu dữ liệu hội thoại.

```text
Renderer
   ↓ IPC bridge
Desktop adapter
   ├─ @hh/session
   ├─ @hh/database
   └─ @hh/provider
```

## 2. Boundary

Package export:

```text
@hh/chat-thread/embed
@hh/chat-thread/desktop
@hh/chat-thread/preload
```

Renderer không import runtime backend block.

Chỉ `desktop/main.ts` gọi public API của Database / Provider / Session.

## 3. Database truth

Session notification không mang content để renderer tin trực tiếp.

```text
Session
   ↓ rowId + conversation
Desktop IPC
   ↓
Renderer
   ↓ query readConversationRow(...)
Database
   ↓
Renderer render truth
```

Conversation row dùng DTO public camelCase:

```text
sessionId
requestId
eventIndex
sortOrder
createdAt
```

Renderer merge snapshot/row mới bằng sortOrder, rồi id để phá hòa.
Không sort chỉ bằng ID: compaction tạo ở request sau có ID lớn nhưng đứng trước
prompt của Session. Notification kết thúc Session đi cùng hàng đợi đọc row,
nên UI hoàn tất các row trước khi bỏ trạng thái đang chạy.

Không tồn tại DTO UI riêng dùng raw SQLite column name.

## 4. Model

UI thao tác model qua desktop bridge.

Desktop adapter chỉ gọi:

```ts
provider.model.list()
provider.model.getSelected()
provider.model.setSelected(...)
provider.model.addCustom(...)
provider.model.getCustom(...)
provider.model.updateCustom(...)
provider.model.deleteCustom(...)
```

ChatThread không biết credential hoặc model runtime routing.

## 5. Request

Renderer gửi:

```text
conversation target
prompt
reasoning-history preference
```

Desktop chuyển sang:

```ts
session.send(target, prompt, options)
```

Renderer không truyền provider/model vào Session.

## 6. Không thuộc ChatThread

```text
context projection
system prompt
token counting
compaction
provider invocation loop
tool execution
database persistence
model runtime routing
API key / endpoint
```

> ChatThread chỉ phát lệnh UI, nhận notification, query public data API và render.
