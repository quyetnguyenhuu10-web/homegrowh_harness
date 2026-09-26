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
    workspace_path,
    refresh,
    timeout_ms);
~~~

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
package/tools/src/tool_definitions.json
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

Timeout process do caller truyền dưới dạng `std::uint32_t timeout_ms`; `0` không
hợp lệ. Sessions lấy giá trị này từ `SessionConfig::tool_result_timeout_ms` sau
khi validate/normalize.

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
package/tools/dist/src/_runtime/tool_host.js
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

## 7. Sandbox process boundary

C++ tool_runtime không spawn target process trực tiếp. Nó tạo sandbox::process_request và gọi sandbox::process(...).

Policy mang:

~~~text
executable
arguments
working directory
stdin
timeout
network capability
filesystem capabilities
refresh
~~~

Process result giữ:

~~~text
started
timed_out
terminated
exit_code
os_error_before_termination
final_error
registry_final_error
path_errors
~~~

Tool runtime không che mất mã lỗi OS khi sandbox/process có lỗi.

Ở public TypeScript facade, process_runner.ts cũng chỉ được phép spawn trusted sandbox broker:

~~~text
<repo>/executable/sandbox_process[.exe]
~~~

Có thể override bằng HOMEGROWPH_SANDBOX_PROCESS.

## 8. refresh semantics

refresh không có nghĩa tạo capability identity mới.

Trong Sessions, refresh_workspace chỉ được consume ở valid tool execution đầu tiên:

~~~cpp
const bool refresh = std::exchange(refresh_pending_, false);
~~~

Sau đó các tool execution còn lại dùng refresh=false.

### Windows refresh=true

Registry:

1. canonicalize path;
2. derive deterministic capability name/SID từ capability signature + canonical path + access mode;
3. nếu registry đã có entry thì kiểm tra name/SID phải khớp;
4. chạy reconcile_tree trên root và descendants;
5. ghi lại durable registry state.

reconcile_acl chỉ bỏ qua khi ACL hiện tại đã compatible. Nếu ACE của capability đã bị xóa bên ngoài, refresh=true sẽ apply lại ACL cho cùng deterministic SID rồi verify lại.

Refresh là thao tác explicit tại thời điểm registration. Không có watcher tự động sửa ACL sau đó.

### Windows refresh=false

reuse:

- yêu cầu capability đã có trong durable registry;
- derive lại deterministic identity để validate;
- không probe ACL;
- không repair ACL;
- không mutate registry state.

Nếu ACL đã bị thay đổi ngoài hệ thống, filesystem operation thật được phép fail và trả lỗi OS gốc.

Release ACL thuộc sandbox registry release/release_all, không thuộc refresh.

## 9. Durable Windows registry

Default state:

~~~text
%LOCALAPPDATA%/<capability_signature>/sandbox-registry.state
~~~

Override:

~~~text
HOMEGROWPH_SANDBOX_REGISTRY_STATE
~~~

Registry state được khóa bằng mutex:

~~~text
Local\HomegrowphHarness.Sandbox.Registry
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

Process/runtime failures được encode thành structured result như:

~~~text
process_timeout
sandbox_registry_failed
sandbox_process_failed
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
sandbox process request
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

> Schema có một nguồn tại package/tools/src/tool_definitions.json; Sessions dùng C++ tool_runtime để thực thi cùng tập tool qua sandbox.
