# BLOCK CHAT THREAD

## 1. Trạng thái source hiện tại

core/chat_thread vẫn tồn tại như Electron/React UI package với tên:

~~~text
@hh/chat-thread
~~~

Các export khai báo trong package.json:

~~~text
@hh/chat-thread/embed
@hh/chat-thread/desktop
@hh/chat-thread/preload
~~~

Tuy nhiên ChatThread hiện chưa được nối sang core C++ mới.

Root CMake hiện tại không add core/chat_thread. core/chat_thread/package.json và desktop/main.ts vẫn tham chiếu các backend TypeScript cũ:

~~~text
@hh/database
@hh/provider
@hh/session
~~~

Trong cây core hiện tại không còn core/database hoặc core/session TypeScript; core/provider hiện là C++ module. Vì vậy không được mô tả adapter này như integration đã hoàn tất với sessions_loop/core C++.

## 2. Source architecture đang có

Code ChatThread hiện tại có ba lớp:

~~~text
React renderer
    ↓
preload bridge
    ↓ IPC
Electron desktop adapter
    ↓
legacy TypeScript backend API imports
~~~

Renderer không import trực tiếp Node/Electron backend API.

preload.ts dùng:

~~~text
contextBridge
ipcRenderer
~~~

để expose ChatThreadDesktopBridge vào renderer.

## 3. Electron app shell

desktop/app.ts hiện:

- gọi app.whenReady();
- install desktop adapter;
- tạo BrowserWindow;
- gắn preload;
- load CHAT_THREAD_RENDERER_URL khi có, nếu không load built renderer index;
- xử lý activate/window-all-closed/before-quit.

BrowserWindow hiện cấu hình:

~~~text
contextIsolation = true
nodeIntegration   = false
sandbox           = false
~~~

Đây là app/window lifecycle của source ChatThread hiện tại.

## 4. Desktop adapter hiện tại

desktop/main.ts đăng ký IPC handler cho:

~~~text
model registry
selected model
conversation list/read/create/delete
repository list/add
context usage
active conversation
send chat request
cancel session
active sessions
session/provider/compaction notifications
~~~

Nó gọi trực tiếp API TypeScript cũ:

~~~text
database.repository.*
database.conversation.*
database.contextUsage.*
providerModel.*
session.send(...)
session.cancel(...)
session.active()
session.subscribe(...)
~~~

Đây là trạng thái source hiện tại của ChatThread, không phải API của core C++ mới.

## 5. IPC event hiện tại

Session event type=row được desktop adapter forward với:

~~~text
repositoryPath
conversationId
sessionId?
requestId?
rowPosition
row
~~~

Tức notification hiện tại có cả row payload; tài liệu cũ nói renderer chỉ nhận rowId rồi bắt buộc query lại Database là không đúng với source này.

Các event khác được forward gồm:

~~~text
session state
context-usage updated
compaction debug
provider error notice
~~~

## 6. Renderer row ordering

HistoryRow hiện chỉ là alias của ConversationRow từ @hh/database cũ.

history_rows.ts merge row bằng:

~~~text
key = rowPosition
sort = rowPosition tăng dần
~~~

Không còn logic sortOrder rồi id trong source hiện tại của ChatThread.

## 7. Model UI hiện tại

Desktop adapter vẫn gọi providerModel:

~~~text
list
addCustom
deleteCustom
getCustom
updateCustom
getSelected
setSelected
~~~

Đây là contract của UI source cũ. C++ core/provider hiện tại không public provider.model.* tương ứng, nên phần này đang là integration gap.

## 8. Send/cancel hiện tại

IPC send hiện gọi:

~~~ts
session.send(
  conversationRef(repositoryPath, conversationId),
  prompt,
  { reasoningHistory }
)
~~~

IPC cancel gọi:

~~~ts
session.cancel(sessionId)
~~~

Khi dispose desktop adapter:

- unsubscribe session event;
- remove toàn bộ IPC handlers;
- gọi session.cancel cho các active session còn lại.

Các API này thuộc backend TypeScript cũ và chưa được thay bằng process/IPC contract tới sessions_loop C++ trong code ChatThread hiện tại.

## 9. Build status

core/chat_thread/package.json vẫn có build:desktop/typecheck chạy:

~~~text
npm --prefix ../database run build
npm --prefix ../provider run build
npm --prefix ../session run build
~~~

Các path/backend assumption này không khớp package graph C++ hiện tại.

Do đó:

~~~text
core/chat_thread = UI source còn tồn tại
root CMake          = không build ChatThread
core C++ process    = chưa được ChatThread source hiện tại launch/connect
~~~

Không nên ghi Electron main hiện đã là thin shell cho core process cho tới khi source thực sự được chuyển sang contract đó.

## 10. Boundary mong muốn không được giả làm trạng thái hiện tại

Core mới hiện có:

~~~text
sessions
provider
events
tool_runtime
sandbox
secrets
~~~

Nhưng ChatThread source chưa dùng các boundary C++ này.

Vì vậy BLOCK_CHAT_THREAD chỉ mô tả phần source UI hiện có và đánh dấu integration gap; không gán cho renderer/main các trách nhiệm mà code chưa triển khai.

> Điểm cần cập nhật tiếp trong code ở một task khác là thay legacy TS backend adapter bằng IPC/process contract tới core C++; task tài liệu hiện tại không sửa phần đó.
