# BLOCK CHAT WORKSPACE

## Trạng thái hiện tại

UI plugin nằm tại:

~~~text
plugins/chat-workspace
~~~

Package hiện chỉ là React/browser plugin. Không còn Electron app shell,
preload, desktop adapter hoặc Electron IPC bridge.

Public bundle hiện tại:

~~~text
@hh/chat-thread/embed
~~~

## Source

Renderer hiện được rút gọn còn ChatContainer:

~~~text
src/
├── chat_container/
├── App.tsx
├── embed.tsx
└── main.tsx
~~~

ChatContainer sở hữu viewport/paging và phần render history nội bộ cần thiết.
Các component navigator, composer, model picker và history adapter cũ đã được
loại bỏ.

## Runtime boundary

Plugin không sở hữu Session core. Hướng tích hợp mới là dùng package dùng chung:

~~~text
@hh/session-client
~~~

Điều khiển đi theo command từ TypeScript tới `session_runtime.exe`; dữ liệu từ
core quay về qua EventPort -> session runtime -> binary IPC -> session client.

~~~text
plugin -> command -> @hh/session-client -> session_runtime.exe
plugin <- event   <- @hh/session-client <- EventPort
~~~

Plugin không cần biết RequestStage/ResponseStage/ToolStage hay API C++ nội bộ.
