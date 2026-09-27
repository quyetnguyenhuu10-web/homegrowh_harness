# BLOCK TOOLS

## 1. Hai public surface hiện tại

Tool stack hiện có hai public entrypoint phục vụ hai loại consumer.

TypeScript facade:

~~~ts
import * as tools from "@hh/tools";
~~~

C++ runtime:

~~~cpp
#include <tool_runtime>
~~~

Sessions C++ gọi:

~~~cpp
tool_runtime::execute(
    tool_call,
    read_files,
    timeout_ms);
~~~

Workspace của tool runtime là current working directory của process. Profile/body
chịu trách nhiệm chọn working directory và quyền trước khi spawn tool_runtime.

@hh/tools vẫn là facade public cho consumer TypeScript; tool_runtime là execution facade mà Sessions dùng.

## 2. Public tools

@hh/tools export đúng bảy hàm:

~~~text
read
write
edit_file
glob
grep
webfetch
todowrite
~~~

Mỗi hàm có dạng:

~~~ts
await tools.read(toolCall, context);
tools.read.definition;
~~~

Root facade không yêu cầu consumer import registry, _shared, read_file hoặc implementation nội bộ.

## 3. Một nguồn schema

Schema public của cả bảy tool nằm tại:

~~~text
core/tools/src/tool_definitions.json
~~~

TypeScript toolDefinition(name) đọc file JSON này và trả structured clone.

Sessions CLI cũng tìm chính tool_definitions.json này, hoặc dùng path từ:

~~~text
TOOLS_DEFINITIONS
~~~

Không có provider-side bản sao schema cần đồng bộ thủ công.

## 4. Runtime registry

C++ tool_runtime đăng ký runtime và capability như sau:

| Tool | Runtime | Filesystem | Network |
| --- | --- | --- | --- |
| read | TypeScript | read_only | none |
| write | TypeScript | read_write | none |
| glob | TypeScript | read_only | none |
| grep | TypeScript | read_only | none |
| webfetch | TypeScript | none | internet_client |
| todowrite | TypeScript | none | none |
| edit_file | native C++ | read_write | none |

Timeout process do profile/body truyền dưới dạng `std::uint32_t timeout_ms`; `0`
không hợp lệ. Sessions không mang tool-result timeout.

Workspace luôn phải là directory tồn tại và được canonicalize trước khi chạy tool.

## 5. TypeScript execution path

Trong C++ tool_runtime, sáu TypeScript tool chạy qua Node tool host:

~~~text
tool_runtime::execute
  ↓
normalize OpenAI function call
  ↓
Node
  ↓
core/tools/dist/src/_runtime/tool_host.js
  ↓
selected TypeScript tool
~~~

Sandbox request cấp read-only cho runtime files, package.json và thư mục Node executable; workspace chỉ được cấp khi tool cần filesystem.

Tool host nhận:

~~~text
tool name
normalized toolCall
repositoryPath
readFiles state
~~~

Sau khi worker thành công, readFiles trả về được merge vào state theo workspace.

## 6. Native edit_file

edit_file là native tool:

~~~text
tool_runtime::execute
  ↓
executable/edit_file[.exe]
  ↓
--toolcall-stdin
~~~

Runtime path mặc định được compile thành stable artifact:

~~~text
<repo>/executable/edit_file.exe    Windows
<repo>/executable/edit_file        Linux
~~~

Có thể override bằng:

~~~text
HOMEGROWPH_EDIT_FILE
~~~

TypeScript facade cũng dùng stable executable/edit_file path thay vì build/Debug hoặc build/Release.

## 7. Execution và permission boundary

Boundary mới tách hoàn toàn execution khỏi permission:

~~~text
sandbox
  = tạo worker process
  = giữ process tree / lifetime

permissions
  = filesystem packet
  = network packet

caller/body
  = tự compose packet
  = tự spawn Node/native process/workload cần chạy
~~~

`permissions` không có process request/result, broker, timeout hoặc executable
riêng. `sandbox` cũng không biết filesystem/network policy.

## 8. Profile owns execution permissions

Sessions không truyền workspace hay policy quyền xuống tool runtime. Body/profile
tự chọn current working directory, filesystem access và network access rồi gọi
native adapter tương ứng trước khi spawn workload.

Refresh/reuse thuộc profile/body, không thuộc Sessions. Body truyền `refresh`
qua process adapter; registry dùng `refresh=true` để reconcile capability và
`refresh=false` để reuse durable capability hiện có.

Nếu ACL đã bị thay đổi ngoài hệ thống, filesystem operation thật được phép fail và trả lỗi OS gốc.

Release ACL thuộc permissions release/release_all, không thuộc refresh.

## 9. Durable Windows registry

Default state:

~~~text
%LOCALAPPDATA%/<capability_signature>/permissions-filesystem.state
~~~

Override:

~~~text
HOMEGROWPH_PERMISSIONS_FILESYSTEM_STATE
~~~

Registry state được khóa bằng mutex:

~~~text
Local\HomegrowphHarness.Permissions.Filesystem
~~~

## 10. Tool result contract

C++ runtime nhận OpenAI-style function tool call:

~~~json
{
  "id": "...",
  "type": "function",
  "function": {
    "name": "...",
    "arguments": "{}"
  }
}
~~~

Runtime normalize arguments, dispatch theo registry rồi trả message role=tool.

Runtime failures được encode thành structured result như:

~~~text
invalid_tool_output
tool_execution_error
unsupported_tool
~~~

Sessions còn bọc kết quả này vào envelope của tool definition trước khi gửi lại model.

## 11. Boundary

Tools stack chịu trách nhiệm:

~~~text
public tool schema
TypeScript tool implementation
native edit_file route
tool dispatch
runtime permission policy
structured tool result
~~~

Tools stack không chịu trách nhiệm:

~~~text
provider request loop
compaction threshold
assistant parsing
session credential ownership
database persistence
UI rendering
~~~

> Schema có một nguồn tại core/tools/src/tool_definitions.json; execution boundary và permission packet là hai khối độc lập.
