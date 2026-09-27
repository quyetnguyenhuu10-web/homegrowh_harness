# BLOCK PROVIDER

## 1. Public boundary

Provider hiện tại là C++ module core/provider.

Public header:

~~~cpp
#include <provider>
~~~

Header này công khai ba phần:

~~~text
request / callback stream
stream
compaction
~~~

Provider không còn public API TypeScript provider.call(...) hoặc provider.model.* trong implementation hiện tại.

## 2. Low-level request

API transport:

~~~cpp
provider::RequestUsage provider::request(
    provider::Provider provider,
    const std::string& url,
    std::string_view api_key,
    const nlohmann::json& body,
    provider::EventSink sink = {});
~~~

EventSink chỉ là callback boundary của Provider:

~~~cpp
struct EventSink {
    void* context = nullptr;
    EventHandler on_event = nullptr;
    FinishedHandler on_finished = nullptr;
};
~~~

Provider enum hiện có:

~~~text
openai
deepseek
bonsai
~~~

request() yêu cầu body.model là string không rỗng. Đây là low-level API nên caller truyền URL và API key trực tiếp.

Khi stream=true:

- OpenAI và DeepSeek được tự thêm stream_options.include_usage=true;
- Bonsai giữ protocol timings riêng;
- nếu response không cung cấp usage, kết quả là UsageState::unavailable.

Transport hiện dùng CPR và parser SSE riêng của Provider.

## 3. Callback stream

SSE parser vẫn thuộc Provider. Mỗi SSE event hoàn chỉnh được chuyển thẳng lên `EventSink::on_event` bằng `std::string&&`.

Provider không còn sở hữu queue, registration, RawResponse hay CompletionPort. Nó cũng không phụ thuộc `event_port`.

Caller quyết định callback làm gì với raw event. Ví dụ Sessions dùng callback như adapter sang EventPort; CLI/test có thể đọc hoặc in trực tiếp mà không cần EventPort.

Nếu callback block thì request/SSE producer tự nhiên bị backpressure cho đến khi callback return. Provider không cần biết nguyên nhân block.

Failure là fail-fast:

- không retry ngầm;
- callback exception được propagate qua request thay vì bị nuốt;
- transport failure được propagate trực tiếp;
- HTTP failure không được giả thành SSE callback event; Provider ném `HttpError` cho control-flow và `HttpError` giữ status_code, status_line, reason và body.

## 4. Usage

RequestUsage là variant:

~~~text
OpenAIUsage
DeepSeekUsage
BonsaiUsage
UsageState::unavailable
~~~

OpenAIUsage giữ prompt/completion/total token cùng các token-detail optional.

DeepSeekUsage giữ prompt/completion/total, cache hit/miss và reasoning detail.

BonsaiUsage đọc timings:

~~~text
cache_n
prompt_n
prompt_ms
prompt_per_token_ms
prompt_per_second
predicted_n
predicted_ms
predicted_per_token_ms
predicted_per_second
~~~

Các type/parser này được generate trực tiếp từ src/request/provider_types.json.
Provider build không phụ thuộc catalog.json.

## 5. Stream helper

Provider có convenience class:

~~~cpp
provider::Stream stream(provider, url, api_key, body);
std::cout << stream;
const provider::RequestUsage& usage = stream.usage();
~~~

Stream:

- ép body.stream=true;
- gọi request() với callback trực tiếp;
- parse từng raw SSE event ngay trong callback;
- chỉ ghi choices[].delta.content ra ostream;
- flush khi có content;
- chỉ được consume một lần;
- usage() chỉ hợp lệ sau khi stream hoàn tất.

Consumer cần raw event, reasoning hoặc tool-call data có thể dùng `request(..., EventSink)` trực tiếp.

## 6. Provider configuration boundary

`provider::request()` và `provider::compaction()` không đọc `catalog.json`.
Provider protocol types được generate chỉ từ `src/request/provider_types.json`.

`core/provider/catalog.json` có thể vẫn được application dùng như một nguồn
config tiện ích, nhưng không phải dependency build/runtime của Provider core.
Caller có thể lấy model configuration từ JSON, SQLite, UI, CLI, remote config
hoặc nguồn khác rồi truyền trực tiếp xuống API.

Một catalog application-level có thể có cấu trúc như:

~~~text
provider
├─ credential
├─ endpoint
└─ models[]
   ├─ id
   └─ context_limit
~~~

endpoint và credential nằm một lần ở provider, không lặp trên từng model.

Provider core không yêu cầu schema catalog này và không resolve model sang
provider/endpoint.

## 7. Credential boundary

Provider low-level nhận api_key dưới dạng std::string_view cho đúng request hiện tại.

Ownership vòng đời secret không nằm trong Provider:

~~~text
Sessions
  -> Secrets giữ credential của session
  -> resolve SecureString khi chuẩn bị request
  -> Provider chỉ dùng view trong thời gian gọi
~~~

Do đó không mô tả Provider là nơi sở hữu selected model, custom-model registry hoặc persistent API-key store; các API TypeScript đó không tồn tại trong core/provider hiện tại.

## 8. Compaction

Compaction hiện là một phần của public Provider package:

~~~cpp
provider::compaction(...)
~~~

Chi tiết transcript, summary request và final request được mô tả trong BLOCK_COMPACTION.md.

> Provider hiện tại chịu trách nhiệm HTTP/SSE/callback streaming/usage và request-level compaction primitive. Provider/endpoint/model/prompt đều là input explicit. Event registration/consumption không thuộc Provider; threshold/policy là quyết định của caller/Sessions configuration.
