# BLOCK SESSION

## 1. Public boundary

Session runtime hiện tại là C++ package package/sessions.

Public header:

~~~cpp
#include <session>
~~~

Source được chia theo trách nhiệm, không gom implementation vào `src/loop`:

~~~text
src/
├─ context/         context estimate, usage accounting, threshold comparison
├─ convert_history/ history conversion theo provider
├─ loop/            optional full-loop driver
├─ request/         provider request adapter + request turn/EventPort bridge
├─ response/        SSE response projection + reasoning extraction
├─ session/         Session ownership, lifecycle state, stage APIs, credential owner
├─ stream/          StreamType + StreamCallback contract
└─ tool/            tool-call preparation/canonicalization/execution adapter
~~~

`src/loop` không sở hữu config/context/credential/response/tool logic. Nó chỉ ghép các stage API thành chế độ chạy tự động.

Public surface chính:

~~~text
sessions::register_session(...)
sessions::declare_request(...) / sessions::run_request(...)
sessions::declare_response(...) / sessions::run_response(...)
sessions::declare_tool(...) / sessions::run_tool(...)
sessions::close_session(...)
sessions::loop(...)
sessions::convert_history(...)
sessions::convert_all_history(...)
~~~

Không còn public TypeScript API session.send/cancel/active/subscribe/close trong package hiện tại.

`Session` giữ toàn bộ state xuyên suốt lifecycle. Mỗi stage được khai báo riêng và chỉ chạy khi caller gọi hàm `run_*` tương ứng. Khoảng giữa `declare_*` và `run_*` là điểm chèn tự nhiên cho CLI/UI/approval/debug.

`sessions::request(...)` không còn nằm trên umbrella public surface; request transport là implementation của request stage.

## 2. Stage API

State machine public:

~~~text
request -> response -> finished
                 \-> tool -> tool -> ... -> request
~~~

Ba stage có type riêng:

~~~cpp
RequestStage  declare_request(Session&);
void          run_request(Session&, RequestStage&&);

ResponseStage declare_response(Session&);
void          run_response(Session&, ResponseStage&&);

ToolStage     declare_tool(Session&);
void          run_tool(Session&, ToolStage&&);
~~~

`declare_request()` chỉ tính context/compact và mô tả request sắp chạy. `run_request()` mới gọi Provider.

`declare_response()` chỉ inspect pending TurnResult. `run_response()` mới commit history/usage và quyết định finished hay tool.

`declare_tool()` chỉ chuẩn hóa một tool call và tạo metadata cho caller inspect. Nó không emit tool event, không consume refresh flag và không gọi runtime. `run_tool()` mới execute đúng một tool.

Mỗi declaration mang generation + token. Declaration mới làm declaration cũ stale; stage đã run không thể reuse.

## 3. Optional full loop

~~~cpp
sessions::LoopResult sessions::loop(
    sessions::SessionConfig&& config);
~~~

LoopResult:

~~~cpp
struct LoopResult {
    nlohmann::json history;
    provider::RequestUsage usage;
};
~~~

Input invariant:

- history phải là array;
- session_current phải là object có messages array không rỗng;
- session_current không được chứa tools;
- tool_definitions phải là array;
- model_id, endpoint và API key không được rỗng;
- context_limit phải lớn hơn 0;
- workspace_path được tool runtime yêu cầu là directory tồn tại và canonical được.

`src/loop/full_loop.cpp` chỉ là automatic driver ghép đúng ba cặp API stage. Nó không chứa một thuật toán orchestration thứ hai.

~~~text
register_session
  ↓
while !finished
  request  -> declare_request  -> run_request
  response -> declare_response -> run_response
  tool     -> declare_tool     -> run_tool
  ↓
close_session
~~~

## 4. Vòng đời orchestration

Một `Session` giữ toàn bộ agent/tool cycle trong RAM:

~~~text
history + session_current
  ↓
declare_request: context facts + threshold comparison
  ↓
run_request: request turn
  ↓
declare/run_response: assistant response commit
  ├─ không có tool_calls -> return LoopResult
  └─ có tool_calls
       ↓
     declare/run_tool từng call
       ↓
     session_current = tool-result messages
       ↓
     request turn tiếp theo
~~~

Sau mỗi provider turn:

~~~text
history = messages thực tế của request
history.push_back(assistant)
~~~

Vì vậy request sau tool result mang theo assistant của request trước, bao gồm reasoning_content và tool_calls đã dựng được, rồi thêm tool-result messages của cycle mới.

Sessions không duy trì một database-backed request state map trong runtime hiện tại. Persistence ra SQLite không nằm trong stage API.

## 5. Context và compaction

Sessions so sánh usage với `compact_threshold` do caller truyền; nó không sở hữu
policy phần trăm cố định.

Cold start:

~~~text
usage_checkpoint =
    estimate(history)
    + estimate(tool_definitions)
~~~

Mỗi vòng:

~~~text
total_usage =
    usage_checkpoint
    + estimate(session_current)
~~~

Decision:

~~~text
total_usage > compact_threshold -> compact
~~~

Không có hardcoded safety policy 95%. `RequestStage` expose `context_usage()` và
`context_limit()` trước khi request chạy, nên caller có thể tự chèn approval,
reject hoặc policy khác giữa `declare_request()` và `run_request()`.

Sau provider response đầu tiên, usage_checkpoint chuyển sang usage thật của request vừa hoàn tất.

Sessions yêu cầu usage thật sau mỗi request. UsageState::unavailable ở vị trí này là lỗi.

Context usage theo provider:

~~~text
OpenAI   -> prompt_tokens + completion_tokens
DeepSeek -> cache_hit + cache_miss + completion_tokens
Bonsai   -> cache_n + prompt_n + predicted_n
~~~

`provider`, `endpoint`, `model_id`, `context_limit`, `compact_threshold`,
`tool_result_timeout_ms`, `session_timeout_ms` và `compaction_prompt` đều được
caller truyền qua `SessionConfig`. Sessions không đọc `catalog.json` và không tự
đọc compaction prompt từ file.

Timeout config dùng milliseconds. Public type là `int`: `-1` nghĩa là max,
số dương là hữu hạn, còn `0` hoặc số nhỏ hơn `-1` là input lỗi. Sau validation,
Sessions normalize sang `std::uint32_t`, nên runtime bên trong không giữ giá trị
âm.

## 6. Request turn

run_turn() tạo:

~~~text
EventPort registration theo stream_id
Provider EventSink adapter cho final request
Provider EventSink adapter cho summary nếu cần
ResponseBuilder
~~~

Request thật chạy trên thread riêng. Provider chỉ gọi callback với từng raw SSE event và không biết EventPort tồn tại.

Callback adapter ở Sessions phát các event `data`, `finished`, `failed` vào EventPort. Thread orchestration block trên `event_port::Read`. Ngay sau mỗi Read, nếu caller truyền `SessionConfig::event_log`, Sessions gọi observer đó với `const event_port::Event&`, rồi mới đưa raw SSE data tương ứng vào ResponseBuilder. Caller không sở hữu Registration và không cần tự Register/Read/Close. Registration nội bộ được tạo trước khi request thread chạy nên event đầu tiên không bị bỏ lỡ.

EventPort dùng single-slot backpressure cho registration này: nếu event trước chưa được orchestration đọc thì callback của Provider block tại Emit; do đó Provider không cần queue/buffer transport riêng mà stream vẫn giữ đúng nhịp consumer.

`event_log` nằm trên cùng execution path sau Read. Vì vậy logger chậm cũng làm chậm việc consume event tiếp theo thay vì biến logging thành fire-and-forget; event quan sát được là chính object EventPort mà turn vừa đọc, không phải payload được serialize/copy lại.

Nếu compact=true và history không rỗng:

~~~text
summary_start
summary_reasoning / summary_content
summary_end
final reasoning / content
~~~

Provider giữ `HttpError` như request error bình thường và không biết EventPort. Sessions không parse raw SSE để đoán lỗi HTTP; nó bắt trực tiếp `provider::HttpError`, đọc các field Provider đã điền rồi emit `EventPort` event `http_error` với `Level::error`, phase summary/request và status_code/status_line/reason/body. Consumer chỉ đọc event `http_error` và forward payload ra `StreamType::http_error`, không phân tích lại nội dung lỗi; exception gốc vẫn được propagate sau khi request thread kết thúc.

Các `std::exception` khác từ Provider, bao gồm transport/network error hiện được
Provider ném dưới dạng `std::runtime_error`, không bị Sessions đổi sang một error
type mới. Sessions emit EventPort event `failed` với `Level::error`, giữ nguyên
`error.what()` trong `data.raw`, rồi vẫn propagate chính exception gốc sau khi
request thread kết thúc. Caller tự quyết định cách phân loại/hiển thị `raw`.

Khi một stage làm Session fail, `fail_session()` không emit EventPort. Nó nhận
`std::exception_ptr`, lưu exception gốc cùng state nơi lỗi xảy ra vào
`SessionData::failure`, rồi thực hiện lifecycle cleanup/close. Tầng `turn` sở hữu
việc publish: `emit_session_failure()` đọc `SessionFailure` và emit
`package="sessions"`, `type="failed"`, `Level::error` với `phase` và `raw`.
Event trả về từ `EventPort::Emit` được chuyển tiếp cho `event_log`; lỗi của
observer không được phép thay thế exception gốc đang propagate.

## 7. Assistant response

ResponseBuilder dựng một assistant message từ stream:

~~~text
role
content
reasoning_content
tool_calls
~~~

Chỉ choices[0] được project vào linear history.

Reasoning:

- DeepSeek: đọc reasoning_content;
- Bonsai: đọc reasoning_content;
- OpenAI Chat Completions: không expose hidden reasoning text nên parser không tự tạo reasoning.

Tool-call arguments dạng string được nối qua các delta. Nếu representation đổi giữa stream thì request lỗi thay vì đoán.

## 8. Tool stage

Nếu assistant có tool_calls, Sessions xử lý tuần tự từng call, nhưng mỗi call là một stage riêng.

`declare_tool()`:

- chọn raw tool call tiếp theo từ assistant history;
- trả `ToolStage` để CLI/UI inspect bằng call_id(), name(), arguments()
  và raw_call();
- không emit `tool_call`;
- không chạy runtime;
- không consume `refresh_pending`.

`run_tool()`:

- chạy executable tool_runtime qua sandbox::process với timeout do caller cấu
  hình;
- nhận canonical tool call và tool result từ stdout; lỗi schema được runtime
  trả trong tool result;
- phát StreamType::tool_call và StreamType::tool_result sau khi nhận kết quả;
- lỗi sandbox, timeout hoặc JSON output sai khiến Session chuyển sang closed và
  exception được trả cho caller;
- thay assistant.tool_calls bằng canonical calls sau tool cuối cùng, trước request tiếp theo.

Tool results của một cycle trở thành:

~~~json
{
  "messages": [
    { "role": "tool", "tool_call_id": "...", "content": "..." }
  ]
}
~~~

và được dùng làm session_current cho vòng kế tiếp.

Session timeout là watchdog thuộc `SessionData`, không phải polling ở stage
boundary. Watchdog bắt đầu cùng Session; nếu deadline hết trước khi lifecycle
được đóng, nó gọi `std::abort()` để hard-stop process Session hiện tại. Vì mỗi
Session chạy trong một process riêng, request Provider, EventPort callback hoặc
tool orchestration có đang block cũng bị OS thu hồi cùng process.

State `finished` chưa cancel watchdog. Timeout chỉ được cancel sau cleanup trong
`fail_session()` hoặc sau credential cleanup thành công trong `close_session()`;
như vậy cleanup bị kẹt vẫn nằm dưới session timeout.

## 9. Workspace refresh

ToolCallHandler giữ refresh_pending từ refresh_workspace.

Tool call đầu tiên được đưa tới sandbox::process consume cờ này:

~~~cpp
const bool refresh = std::exchange(refresh_pending_, false);
~~~

Runtime kiểm tra definition và arguments sau khi process đã bắt đầu, vì vậy
những call sai schema cũng consume refresh.

Sau lần runtime đầu tiên, mọi tool execution còn lại trong cùng Session dùng refresh=false.

## 10. Credential ownership

register_session() nhận ownership raw API key qua SessionConfig.

Đầu vòng đời:

~~~text
raw key
  ↓
Secrets::set_session(random session signature)
  ↓
wipe raw string
~~~

Signature có dạng:

~~~text
HomegrowphHarness/Session/<random hex>
~~~

Mỗi request resolve key thành SecureString rồi chỉ truyền view xuống Provider.

Khi loop kết thúc, credential session được erase. Nếu đang xử lý primary exception, cleanup failure được giữ như secondary error và không được phép thay thế lỗi chính.

## 10. Stream contract

StreamType hiện có:

~~~text
reasoning
content
summary_start
summary_reasoning
summary_content
summary_end
http_error
secondary_error
tool_call
tool_result
context_usage
~~~

context_usage mang JSON:

~~~json
{
  "used": 123,
  "limit": 456
}
~~~

Session stream là observer callback; history hoàn chỉnh vẫn được trả bằng LoopResult.

## 11. CLI

Executable hiện tại:

~~~text
sessions_loop.exe <session_current> <id> <workspace_path>
sessions_loop.exe <history> <session_current> <id> <workspace_path>
~~~

history và session_current có thể là inline JSON hoặc path tới JSON file.

Environment:

~~~text
HH_API_KEY=<raw api key>
HH_PROVIDER=<openai|deepseek|bonsai>
HH_ENDPOINT=<request endpoint>
HH_CONTEXT_LIMIT=<tokens>
HH_COMPACT_THRESHOLD=<tokens>
PROVIDER_COMPACTION_PROMPT=<prompt file path>
HH_REFRESH_WORKSPACE=<true|false>     default false
TOOLS_DEFINITIONS=<optional path>
~~~

Nếu TOOLS_DEFINITIONS không set, CLI tìm tool_definitions.json từ working directory đi lên.

## 12. Boundary

Sessions hiện sở hữu:

~~~text
request/tool loop
context usage accounting + caller-provided threshold comparison
assistant stream projection
Provider-to-EventPort adapter và registration lifecycle
tool-call canonicalization
per-loop credential lifetime
workspace-refresh consumption
~~~

Sessions hiện không sở hữu:

~~~text
SQLite persistence trong loop
Electron/UI state
selected-model UI registry
provider/model/endpoint resolution
context safety/approval policy
compaction prompt loading
transport implementation
EventPort queue/wakeup primitive
tool implementation
sandbox ACL implementation
~~~

> Sessions là C++ orchestration state machine; Provider làm HTTP/SSE, EventPort cung cấp registration/read/backpressure, Tool Runtime chạy tool và Secrets giữ credential.
