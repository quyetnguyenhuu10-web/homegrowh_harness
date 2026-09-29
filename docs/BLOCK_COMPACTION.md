# BLOCK COMPACTION

## 1. Vị trí hiện tại

Compaction không còn là package TypeScript @hh/compaction riêng.

Implementation hiện nằm trong lib/provider/src/compaction và được export qua public header:

~~~cpp
#include <provider>
~~~

Public API:

~~~cpp
provider::CompactionResult provider::compaction(
    provider::Provider selected_provider,
    const std::string& endpoint,
    const std::string& model_id,
    std::string_view api_key,
    std::string_view compaction_prompt,
    const nlohmann::json& session_current,
    const nlohmann::json& tool_definitions,
    const nlohmann::json& history = {},
    provider::EventSink response_sink = {},
    bool compact = false,
    provider::EventSink summary_sink = {},
    provider::CompactionResponse* compaction_response = nullptr);
~~~

## 2. Result

~~~cpp
struct CompactionResponse {
    RequestUsage usage = UsageState::unavailable;
};

struct CompactionResult {
    nlohmann::json messages;
    RequestUsage usage = UsageState::unavailable;
};
~~~

CompactionResponse giữ usage của request tóm tắt. Raw summary stream đi qua `summary_sink`; final raw stream đi qua `response_sink`.

CompactionResult.messages là message list thực tế được gửi vào final model request; usage là usage của final request, không phải usage của summary request.

## 3. Khi compact=false

Nếu compact=false, hoặc history rỗng, function không tạo summary.

Nó ghép:

~~~text
history
+ session_current.messages
~~~

thành body:

~~~json
{
  "model": "...",
  "messages": ["..."],
  "stream": true
}
~~~

Nếu tool_definitions không rỗng thì final request có thêm tools.

Sau đó gọi provider::request(...) và trả lại đúng request messages cùng usage.

## 4. Khi compact=true

Compaction thật chỉ xảy ra khi:

~~~text
compact == true
AND history không rỗng
~~~

Flow hiện tại:

~~~text
history
  ↓
build_transcript(history)
  ↓
append compaction_prompt do caller truyền
  ↓
summary request
  ↓
gom choices[].delta.content thành summary
  ↓
[{ role: "user", content: summary }]
  + session_current.messages
  ↓
final request
~~~

Summary request:

- dùng provider, endpoint và model_id caller truyền;
- stream=true;
- không gửi tool definitions;
- mỗi raw SSE event vừa được dùng để aggregate summary vừa được forward qua summary_sink nếu caller truyền;
- bắt buộc có usage;
- bắt buộc tạo summary content không rỗng.

Final request:

- dùng compacted summary làm user message đầu;
- append nguyên session_current.messages sau summary;
- gửi tool_definitions nếu caller truyền;
- usage cuối được trả trong CompactionResult.

## 5. Transcript format

build_transcript(history) tạo đúng một user message chứa transcript text.

Assistant:

~~~text
<assistant>
<thinking>
reasoning_content
</thinking>
content
<tool_call>
[raw tool_calls JSON]
</tool_call>
</assistant>
~~~

thinking chỉ xuất hiện khi reasoning_content có giá trị.

User:

~~~text
<user>
content
</user>
~~~

Tool message:

~~~text
<user>
<tool_results>
{full tool message JSON}
</tool_results>
</user>
~~~

Compaction không tự cắt một tail riêng bên trong history. Caller quyết định history nào được đưa vào.

## 6. Prompt

Provider core không tự đọc prompt từ filesystem hay environment.

`compaction_prompt` là input explicit của API. File sau có thể được caller dùng
như một resource mặc định:

~~~text
lib/provider/src/compaction/COMPACTION.md
~~~

Ví dụ executable/ứng dụng có thể tự đọc path từ environment:

~~~text
PROVIDER_COMPACTION_PROMPT
~~~

Nhưng việc đọc file/environment nằm ngoài Provider. Prompt truyền vào hiện có
thể yêu cầu tạo continuation context ngắn, giữ decisions, constraints,
unresolved work, tool results, identifiers và facts cần để tiếp tục.

## 7. Threshold là input của Session

Provider::compaction không sở hữu threshold. Sessions cũng không hardcode phần
trăm 80/95.

Sessions tính:

~~~text
total_usage = usage_checkpoint + estimate(session_current)
~~~

Quyết định compact hiện tại:

~~~text
total_usage > compact_threshold
    -> compact = true
~~~

`compact_threshold` là số token tuyệt đối do caller truyền khi đăng ký Session.
Nếu caller muốn 80%, 70% hoặc ngưỡng theo model thì caller tự tính trước.
Sessions không có safety cutoff 95%; caller có thể đọc usage ở RequestStage và
tự approve/reject trước `run_request()`.

Cold start dùng estimate(history) + estimate(tool definitions) làm checkpoint. Sau request đầu, Sessions dùng usage thật của provider làm checkpoint cho vòng sau.

## 8. Boundary

Compaction chịu trách nhiệm:

~~~text
history -> transcript
use caller-provided summary prompt
summary request
summary stream aggregation
summary + current-session merge
final provider request
summary/final usage separation
~~~

Compaction không chịu trách nhiệm:

~~~text
quyết định threshold
context safety limit
tool execution
workspace sandbox
database persistence
session lifecycle
~~~

> Caller truyền quyết định compact; Provider thực hiện đúng request path tương ứng.
