# BLOCK TOOLS

## 1. Hai public surface hiện tại

Tool stack hiện có hai public entrypoint phục vụ hai loại consumer.

TypeScript facade:

~~~ts
import * as tools from "@hh/tools";
~~~

C++ Sessions chạy executable `tool_runtime` qua `sandbox::process`, gửi tool
call dưới dạng JSON qua stdin và nhận kết quả JSON từ stdout. `tool_runtime`
không còn header hoặc hàm C++ public để include. @hh/tools vẫn là facade public
cho consumer TypeScript.

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

Build tổng đóng gói file này cạnh executable `tool_runtime` tại
`executable/tool_runtime_tools/tool_definitions.json`. Runtime có thể đọc file
khác qua:

~~~text
HOMEGROWPH_TOOL_DEFINITIONS
~~~

Không có provider-side bản sao schema cần đồng bộ thủ công.

## 4. Runtime registry

Executable tool_runtime đăng ký worker như sau:

| Tool | Worker |
| --- | --- |
| read | TypeScript/Node |
| write | TypeScript/Node |
| glob | TypeScript/Node |
| grep | TypeScript/Node |
| webfetch | TypeScript/Node |
| todowrite | TypeScript/Node |
| edit_file | native C++ executable |

Sessions lấy timeout process từ `SessionConfig::tool_result_timeout_ms` sau khi
validate/normalize. Quyền filesystem và network do caller khai báo trong
`SessionConfig::sandbox_config` cho cả process tool_runtime; runtime không tự
đổi quyền sandbox theo tên tool.

Sessions CLI kiểm tra và canonicalize workspace trước khi tạo SessionConfig.

## 5. TypeScript execution path

Sáu TypeScript tool chạy qua Node tool host:

~~~text
sessions::run_tool
  ↓
sandbox::process(tool_runtime)
  ↓
validate và normalize OpenAI function call
  ↓
Node
  ↓
tool_runtime_tools/src/_runtime/tool_host.js
  ↓
selected TypeScript tool
~~~

Sandbox config được áp khi Sessions khởi chạy tool_runtime. Worker Node là
child process do tool_runtime tạo.

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
sessions::run_tool
  ↓
sandbox::process(tool_runtime)
  ↓
executable/edit_file[.exe]
  ↓
--toolcall-stdin
~~~

Runtime tìm edit_file cạnh executable tool_runtime:

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

Sessions tạo `sandbox::process_request` rồi gọi `sandbox::process(...)` để chạy
executable tool_runtime. Tool runtime dùng process API nội bộ để chạy Node hoặc
edit_file dưới quyền của process đã được tạo.

Request của Sessions mang:

~~~text
executable
working directory
stdin
timeout
config gồm filesystem và network
refresh
~~~

`request.results` giữ:

~~~text
started
timed_out
terminated
exit_code
os_error_before_termination
final_error
config.final_error
config.path_errors
~~~

Lỗi config, launch, timeout hoặc output sai được Sessions báo bằng exception;
nếu runtime đã trả JSON hợp lệ thì lỗi nghiệp vụ của tool nằm trong tool
result. Không còn executable broker `sandbox_process`.

## 8. refresh semantics

refresh không có nghĩa tạo capability identity mới.

Trong Sessions, refresh_workspace được consume khi chạy tool call đầu tiên:

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

Release ACL thuộc registry nội bộ của sandbox, không thuộc refresh.

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

Executable tool_runtime nhận OpenAI-style function tool call:

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

Runtime normalize arguments, kiểm tra schema, dispatch theo registry nội bộ và
trả JSON chứa `tool_call` canonical cùng `result` dạng message role=tool.

Lỗi do runtime tạo được encode trong `result.content`, ví dụ:

~~~text
invalid_tool_call
tool_schema_not_found
invalid_arguments
invalid_tool_output
tool_execution_error
unsupported_tool
~~~

Runtime tự bọc kết quả vào envelope của tool definition. Timeout và lỗi
sandbox::process xảy ra trước khi runtime trả JSON được Sessions báo bằng
exception.

## 11. Boundary

Tools stack chịu trách nhiệm:

~~~text
public tool schema
TypeScript tool implementation
native edit_file route
tool dispatch
structured tool result
~~~

Sessions sở hữu sandbox config và process request để khởi chạy tool_runtime.

Tools stack không chịu trách nhiệm:

~~~text
provider request loop
compaction threshold
assistant parsing
session credential ownership
database persistence
UI rendering
~~~

> Schema có một nguồn tại package/tools/src/tool_definitions.json; Sessions chạy executable tool_runtime qua sandbox để thực thi cùng tập tool.
