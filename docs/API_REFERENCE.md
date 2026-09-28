# API Reference

Tài liệu này là danh mục API public đang có trong thư mục package.

## 1. Quy ước public

Các điểm include hoặc giao tiếp được hỗ trợ:

| Package          | Header                                               | Namespace chính      | CMake target                         |
| ---------------- | ---------------------------------------------------- | -------------------- | ------------------------------------ |
| Context usage    | <code>&lt;context_usage&gt;</code>                   | context_usage        | context_usage::context_usage         |
| Event port       | <code>&lt;event_port&gt;</code>                      | event_port           | event_port::event_port               |
| Provider         | <code>&lt;provider&gt;</code>                        | provider             | provider                             |
| Sandbox          | <code>&lt;config.h&gt;</code>, <code>&lt;process_request.h&gt;</code>, <code>&lt;process_results.h&gt;</code>, <code>&lt;sandbox_process.h&gt;</code> | sandbox | sandbox |
| Secrets          | <code>&lt;secrets&gt;</code>                         | secrets              | secrets::secrets                     |
| Sessions         | <code>&lt;sessions&gt;</code>                        | sessions             | sessions::sessions                   |
| Tool runtime     | Không có header C++ public; giao tiếp JSON qua executable | process boundary | tool_runtime (executable) |
| Filesystems      | <code>&lt;fsystem&gt;</code>                         | fsystem              | filesystems                          |
| Toàn bộ C++ core | <code>&lt;homegrowh_harness&gt;</code>              | các namespace thư viện | homegrowh_harness::homegrowh_harness |

Ví dụ include toàn bộ API thư viện C++ trong CLI bên ngoài:

~~~cpp
#include <homegrowh_harness>
~~~

Target umbrella là INTERFACE; nó link các thư viện C++ core. Header
<code>&lt;homegrowh_harness&gt;</code> gom các header public của những thư viện
đó. Executable tool_runtime dùng giao tiếp JSON nên không có header C++ trong
umbrella. Dependency bên thứ ba được CMake gốc quản lý trong cùng build tree.

Các header nằm trong src, detail, platform, tests hoặc codegen không được xem
là API ổn định, dù một số include directory được expose để package khác biên
dịch. Consumer không nên include trực tiếp các header đó.

Các API dùng nlohmann::json cần include dependency JSON thông qua target CMake;
không copy generated header vào source tree.

### Mô hình kiến trúc

Core C++ theo hướng thư viện trước, với tầng điều phối có thể thay thế. Mỗi
thư viện sở hữu một capability và công khai contract qua header public. Package
khác gọi contract đó; implementation, storage và platform backend nằm phía sau
boundary của package. tool_runtime là executable phục vụ giao tiếp qua process.

Sessions cũng chỉ là một package trong hệ thống. Session kernel cung cấp state,
invariant và các stage API. sessions::loop là driver mặc định để chạy workflow
request, response và tool; CLI, UI, scheduler hoặc driver khác có thể dùng
trực tiếp stage API thay cho loop.

Đây là kiến trúc package có API thay thế được ở mức build/link. Project hiện
chưa có plugin loader runtime, manifest discovery hoặc ABI động cho từng
package. Muốn load DLL/so độc lập sau này có thể đặt plugin manager lên trên
những contract hiện tại.

## 2. context_usage

Header:

~~~cpp
#include <context_usage>
~~~

Namespace: context_usage.

### Hàm ước lượng context

~~~cpp
std::uint64_t estimate(const nlohmann::json& body);
~~~

body có thể là:

- message array: ước lượng trực tiếp phần JSON đã serialize;
- session object: chỉ lấy hai trường messages và tools nếu có rồi ước lượng.

Đây là ước lượng nhanh, không phải tokenizer chính xác của provider. Dữ liệu
object không hợp lệ hoặc messages/tools không phải array ném
std::invalid_argument. Dữ liệu gây tràn phép cộng ném std::overflow_error.

### Tính usage chính xác theo provider

~~~cpp
std::uint64_t openai(
    std::uint64_t prompt_tokens,
    std::uint64_t completion_tokens);

std::uint64_t deepseek(
    std::uint64_t prompt_cache_hit_tokens,
    std::uint64_t prompt_cache_miss_tokens,
    std::uint64_t completion_tokens);

std::uint64_t bonsai(
    std::uint64_t cache_n,
    std::uint64_t prompt_n,
    std::uint64_t predicted_n);
~~~

Các hàm trả tổng token theo quy tắc của provider và ném std::overflow_error
nếu tổng vượt uint64_t.

## 3. event_port

Header:

~~~cpp
#include <event_port>
~~~

event_port là cổng event trong tiến trình, có registration theo bộ lọc và
single-slot backpressure.

### Kiểu dữ liệu

~~~cpp
enum class Level
{
    trace,
    debug,
    info,
    warning,
    error,
    critical
};

struct Reference
{
    std::string type;
    std::string value;

    Reference(std::string&& type, std::string&& value) noexcept;
    Reference(const Reference&) = delete;
    Reference& operator=(const Reference&) = delete;
    Reference(Reference&&) noexcept = default;
    Reference& operator=(Reference&&) noexcept = default;
};

using References = std::deque<Reference>;

struct Event;
using EventPtr = std::shared_ptr<const Event>;
~~~

Reference chỉ nhận ownership bằng rvalue string và không copy được. EventPtr
trỏ tới event bất biến về mặt public; Event cũng không copy hoặc move được.

### Registration và operation

~~~cpp
class Registration
{
public:
    ~Registration();
    Registration(const Registration&) = delete;
    Registration& operator=(const Registration&) = delete;
    Registration(Registration&&) noexcept;
    Registration& operator=(Registration&&) noexcept;
};

struct Register
{
    std::string package;
    References references;
};

struct Read
{
    Registration& registration;
};

struct Emit
{
    std::string package;
    Level level = Level::info;
    std::string type;
    References references;
    nlohmann::json data;
};

struct Close
{
    Registration& registration;
};

template <typename Operation>
auto port(Operation&& operation);
~~~

Ví dụ:

~~~cpp
event_port::Registration registration =
    event_port::port(event_port::Register{
        "sessions",
        event_port::References{
            event_port::Reference{"session", "abc"}
        }
    });

event_port::EventPtr event = event_port::port(
    event_port::Read{registration});

event_port::EventPtr emitted = event_port::port(
    event_port::Emit{
        "sessions",
        event_port::Level::info,
        "content",
        {},
        {{"delta", "hello"}}
    });

event_port::port(event_port::Close{registration});
~~~

Quy tắc:

- Register trả về Registration. package rỗng là wildcard; mọi Reference trong
  registration phải xuất hiện với cùng type và value trong event.
- Read chờ blocking đến khi có event phù hợp. Khi registration bị hủy và không
  còn event chờ, nó ném std::logic_error.
- Emit yêu cầu package, type không rỗng và reference type không rỗng. Event
  nhận sequence tăng dần và timestamp system_clock trước khi phát.
- Close đóng registration theo kiểu graceful: đánh thức reader/writer đang chờ
  nhưng không xóa pending event. Reader được phép drain pending cuối cùng; Read
  tiếp theo khi closed và không còn pending sẽ ném std::logic_error.
- Mỗi registration chỉ giữ một event chờ. Nếu consumer chưa đọc event trước,
  bên emit phù hợp sẽ chờ; đây là backpressure chủ động.
- Hủy Registration là hard cleanup: đánh thức các bên đang chờ, đóng
  registration và bỏ pending nếu còn. Dùng Close khi caller cần graceful drain.

Operation lvalue hoặc operation không được hỗ trợ gây lỗi compile-time.
Registration đã move gây std::logic_error.

### Event

~~~cpp
struct Event
{
    std::uint64_t sequence = 0;
    std::chrono::system_clock::time_point timestamp;
    std::string package;
    Level level = Level::info;
    std::string type;
    References references;
    nlohmann::json data;
};
~~~

## 4. provider

Header:

~~~cpp
#include <provider>
~~~

provider cung cấp kiểu provider, parser usage được generate từ
package/provider/src/request/provider_types.json, HTTP/SSE request, stream
helper và compaction.

### Provider và usage

~~~cpp
enum class Provider
{
    openai,
    deepseek,
    bonsai
};

enum class UsageState
{
    unavailable
};

using RequestUsage = std::variant<
    OpenAIUsage,
    DeepSeekUsage,
    BonsaiUsage,
    UsageState>;
~~~

OpenAIUsage:

~~~cpp
struct OpenAIUsage
{
    struct PromptTokensDetails
    {
        std::optional<std::uint64_t> audio_tokens;
        std::optional<std::uint64_t> cache_write_tokens;
        std::optional<std::uint64_t> cached_tokens;
        std::optional<std::uint64_t> image_tokens;
        std::optional<std::uint64_t> text_tokens;
    };

    struct CompletionTokensDetails
    {
        std::optional<std::uint64_t> accepted_prediction_tokens;
        std::optional<std::uint64_t> audio_tokens;
        std::optional<std::uint64_t> reasoning_tokens;
        std::optional<std::uint64_t> rejected_prediction_tokens;
        std::optional<std::uint64_t> text_tokens;
    };

    std::uint64_t completion_tokens = 0;
    std::uint64_t prompt_tokens = 0;
    std::uint64_t total_tokens = 0;
    std::optional<PromptTokensDetails> prompt_tokens_details;
    std::optional<CompletionTokensDetails> completion_tokens_details;
};
~~~

DeepSeekUsage:

~~~cpp
struct DeepSeekUsage
{
    struct PromptTokensDetails
    {
        std::optional<std::uint64_t> cached_tokens;
    };

    struct CompletionTokensDetails
    {
        std::optional<std::uint64_t> reasoning_tokens;
    };

    std::uint64_t completion_tokens = 0;
    std::uint64_t prompt_tokens = 0;
    std::optional<PromptTokensDetails> prompt_tokens_details;
    std::uint64_t prompt_cache_hit_tokens = 0;
    std::uint64_t prompt_cache_miss_tokens = 0;
    std::uint64_t total_tokens = 0;
    std::optional<CompletionTokensDetails> completion_tokens_details;
};
~~~

BonsaiUsage:

~~~cpp
struct BonsaiUsage
{
    std::uint64_t cache_n = 0;
    std::uint64_t prompt_n = 0;
    double prompt_ms = 0.0;
    double prompt_per_token_ms = 0.0;
    double prompt_per_second = 0.0;
    std::uint64_t predicted_n = 0;
    double predicted_ms = 0.0;
    double predicted_per_token_ms = 0.0;
    double predicted_per_second = 0.0;
};
~~~

std::optional có nghĩa field có thể vắng hoặc có JSON null. Field required
dùng giá trị số mặc định của struct nhưng parser vẫn yêu cầu field đó có
trong JSON.

### Parser được generate

~~~cpp
Provider provider_from_name(
    const std::string& provider_name);

std::optional<nlohmann::json> usage_from_event(
    Provider provider,
    const nlohmann::json& event);

RequestUsage parse_usage(
    Provider provider,
    const nlohmann::json& usage);
~~~

provider_from_name nhận openai, deepseek, bonsai; tên khác ném
std::runtime_error.

usage_from_event đọc trường usage cho OpenAI/DeepSeek và timings cho Bonsai.
Nếu trường không có hoặc là null, hàm trả std::nullopt.

parse_usage chuyển object usage thành struct tương ứng. Field required thiếu
hoặc sai kiểu sẽ ném exception của nlohmann::json; provider không hợp lệ gây
std::logic_error.

### HTTP request và callback

~~~cpp
struct HttpError final : std::runtime_error
{
    HttpError();

    HttpError(
        long status_code,
        std::string&& status_line,
        std::string&& reason,
        std::string&& body);

    long status_code = 0;
    std::string status_line;
    std::string reason;
    std::string body;
};

using EventHandler = void (*)(void*, std::string&&);
using FinishedHandler = void (*)(void*);

struct EventSink
{
    void* context = nullptr;
    EventHandler on_event = nullptr;
    FinishedHandler on_finished = nullptr;
};

RequestUsage request(
    Provider provider,
    const std::string& url,
    std::string_view api_key,
    const nlohmann::json& body,
    EventSink sink = {});
~~~

body.model phải là string không rỗng. Caller truyền endpoint và API key trực
tiếp. Với stream=true, OpenAI và DeepSeek được bổ sung
stream_options.include_usage=true; Bonsai dùng timings.

Mỗi SSE event hoàn chỉnh được chuyển qua EventSink::on_event dưới dạng
std::string&&. Sau khi request kết thúc, on_finished được gọi nếu có.
Callback exception và lỗi transport được propagate. HTTP failure ném
HttpError, không biến thành event callback. Nếu response không có usage,
kết quả là UsageState::unavailable.

### Stream helper

~~~cpp
class Stream
{
public:
    Stream(
        Provider provider,
        const std::string& url,
        const std::string& api_key,
        const nlohmann::json& body);

    ~Stream();
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;
    Stream(Stream&&) noexcept;
    Stream& operator=(Stream&&) noexcept;

    const RequestUsage& usage() const;
};

std::ostream& operator<<(std::ostream& output, Stream& stream);
~~~

Constructor ép body.stream=true. operator<< tiêu thụ stream đúng một lần,
chỉ ghi choices[].delta.content ra ostream và flush khi có content.
usage() chỉ hợp lệ sau khi stream đã được tiêu thụ; gọi sớm, gọi sau move
hoặc consume lần hai ném std::logic_error.

### Compaction

~~~cpp
struct CompactionResponse
{
    RequestUsage usage = UsageState::unavailable;
};

struct CompactionResult
{
    nlohmann::json messages;
    RequestUsage usage = UsageState::unavailable;
};

CompactionResult compaction(
    Provider selected_provider,
    const std::string& endpoint,
    const std::string& model_id,
    std::string_view api_key,
    std::string_view compaction_prompt,
    const nlohmann::json& session_current,
    const nlohmann::json& tool_definitions,
    const nlohmann::json& history = {},
    EventSink response_sink = {},
    bool compact = false,
    EventSink summary_sink = {},
    CompactionResponse* compaction_response = nullptr);
~~~

Khi compact=false hoặc history rỗng, hàm ghép history với
session_current.messages rồi gửi final request.

Khi compact=true và history không rỗng, hàm gửi summary request, gom
choices[].delta.content, tạo một user message summary rồi gửi final request.
summary_sink nhận raw summary event; response_sink nhận raw final event.
CompactionResponse::usage là usage của summary request; kết quả trả về giữ
usage của final request.

Provider, endpoint, model_id và compaction_prompt đều do caller truyền trực
tiếp. Provider core không resolve model qua catalog và không tự đọc prompt từ
filesystem/environment. Summary rỗng hoặc usage không có sẽ ném exception.

## 5. sandbox

Các header public:

~~~cpp
#include <config.h>
#include <process_request.h>
#include <process_results.h>
#include <sandbox_process.h>
~~~

Target CMake là thư viện <code>sandbox</code>. Implementation chọn theo hệ
điều hành: Windows dùng AppContainer, Job Object và ACL; Linux dùng Landlock
và process group. Không có broker executable <code>sandbox_process</code>.

### Khai báo quyền bằng config

~~~cpp
enum class network_access : std::uint32_t
{
    none = 0,
    internet_client = 1,
};

// Các factory trả về option dùng cho config.
detail::read_only_config read_only(std::filesystem::path path);
detail::read_write_config read_write(std::filesystem::path path);
detail::network_config network(network_access access) noexcept;

using config_option = std::variant<
    detail::read_only_config,
    detail::read_write_config,
    detail::network_config>;

class config final
{
public:
    config() = default;
    config(std::initializer_list<config_option> options);
    config& add(config_option option);
};
~~~

<code>read_only(path)</code>, <code>read_write(path)</code> và
<code>network(access)</code> tạo option; caller ghép bằng initializer list hoặc
<code>config.add(...)</code>. Path rỗng, cùng path được khai báo cả quyền đọc
và ghi, hoặc các network policy khác nhau sẽ ném
<code>std::invalid_argument</code>. Khai báo lặp cùng quyền được giữ một lần;
không thêm network option thì mặc định là <code>none</code>.

Ví dụ:

~~~cpp
sandbox::config permissions{
    sandbox::read_only(executable),
    sandbox::read_write(workspace),
    sandbox::network(sandbox::network_access::none),
};
~~~

Registry capability vẫn được dùng bên trong sandbox để áp filesystem config.
Các hàm <code>registry</code>, <code>release</code> và
<code>release_all</code> nằm trong header <code>src/registry.h</code>, không
thuộc API public cho consumer.

### Process API

~~~cpp
struct config_path_error
{
    std::filesystem::path path;
    std::error_code error;
};

struct config_results
{
    std::error_code final_error;
    std::vector<config_path_error> path_errors;
};

struct process_final_state
{
    bool started = false;
    bool timed_out = false;
    bool terminated = false;
    int exit_code = -1;
    std::error_code os_error_before_termination;
    std::error_code final_error;
    config_results config;
};

struct process_results
{
    process_final_state state;
    std::string stdout_text;
    std::string stderr_text;
};

struct process_request
{
    std::filesystem::path executable;
    std::filesystem::path working_directory;
    sandbox::config config;
    std::string stdin_data;
    std::chrono::milliseconds timeout{120000};
    bool refresh = false;
    process_results results;
};

void process(process_request& request);
~~~

<code>process(request)</code> chạy executable và ghi kết quả vào
<code>request.results</code>. Input gồm working directory, stdin, timeout và
config quyền; không có trường arguments. <code>refresh=true</code> yêu cầu
registry refresh/reconcile quyền filesystem, còn <code>false</code> chỉ reuse
registration đã có. <code>state.config</code> giữ lỗi áp config theo từng path
và lỗi chung; <code>state.final_error</code> giữ lỗi launch/supervision.
<code>timed_out</code>, <code>terminated</code> và
<code>os_error_before_termination</code> mô tả trạng thái khi timeout.

~~~cpp
sandbox::process_request request;
request.executable = executable;
request.working_directory = workspace;
request.config = permissions;
request.stdin_data = "{}";
sandbox::process(request);
const sandbox::process_results& result = request.results;
~~~

## 6. secrets

Header:

~~~cpp
#include <secrets>
~~~

Backend chọn Windows Credential Manager hoặc Linux credential backend theo
platform.

### Kết quả credential

~~~cpp
enum class SecretStatus
{
    success,
    not_found,
    failed,
};

struct SecretError
{
    std::uint32_t code = 0;
    std::string operation;
};

struct SecretResult
{
    SecretStatus status = SecretStatus::failed;
    std::string value;
    SecretError error;
};

struct SecretOperationResult
{
    SecretStatus status = SecretStatus::failed;
    SecretError error;
};
~~~

### Credential API

~~~cpp
SecretResult get(const std::string& signature);

std::string resolve(const std::string& signature);

SecureString resolve_secure(const std::string& signature);

SecureString resolve_secure_session(const std::string& signature);

SecretOperationResult set(
    const std::string& signature,
    const std::string& value);

SecretOperationResult set_session(
    const std::string& signature,
    const std::string& value);

SecretOperationResult erase(const std::string& signature);

SecretOperationResult erase_session(const std::string& signature);
~~~

get trả trạng thái đầy đủ. resolve và hai hàm resolve_secure chuyển lỗi thành
exception: std::system_error nếu có mã OS, nếu không là std::runtime_error.
set, erase và bản _session trả SecretOperationResult, không ném lỗi nghiệp vụ
thông thường.

Bản thường và bản _session dùng vùng lưu trữ khác nhau theo backend. Session
credential được Sessions tạo tạm trong vòng đời loop.

### SecureString

~~~cpp
class SecureString final
{
public:
    SecureString() noexcept = default;
    SecureString(const SecureString&) = delete;
    SecureString& operator=(const SecureString&) = delete;
    SecureString(SecureString&& other) noexcept;
    SecureString& operator=(SecureString&& other) noexcept;

    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::string_view view() const noexcept;
};

SecureString secure_string_from(std::string& value);
~~~

SecureString move-only; buffer được zero trước khi giải phóng. view() chỉ có
giá trị trong lifetime của SecureString. secure_string_from yêu cầu source
string không rỗng, copy vào secure buffer rồi wipe source string.

## 7. sessions

Header:

~~~cpp
#include <sessions>
~~~

Sessions là orchestration layer cho provider request, tool cycle, caller-provided
context threshold và history projection.

### Kiểu stream và kết quả

~~~cpp
enum class StreamType
{
    reasoning,
    content,
    summary_start,
    summary_reasoning,
    summary_content,
    summary_end,
    http_error,
    secondary_error,
    tool_call,
    tool_result,
    context_usage,
};

using StreamCallback =
    std::function<void(StreamType type, std::string_view delta)>;

using EventLogCallback =
    std::function<void(const event_port::Event& event)>;

struct LoopResult
{
    nlohmann::json history;
    provider::RequestUsage usage = provider::UsageState::unavailable;
};
~~~

### Session kernel và stage API

Session kernel là public API độc lập với driver loop:

~~~cpp
enum class SessionState
{
    request,
    response,
    tool,
    finished,
    closed,
};

struct SessionConfig
{
    std::string api_key_raw;
    nlohmann::json history = nlohmann::json::array();
    nlohmann::json session_current;
    nlohmann::json tool_definitions = nlohmann::json::array();
    provider::Provider provider = provider::Provider::openai;
    std::string endpoint;
    std::string model_id;
    std::uint64_t context_limit = 0;
    std::uint64_t compact_threshold = 0;
    int tool_result_timeout_ms = -1;
    int session_timeout_ms = -1;
    std::string compaction_prompt;
    std::filesystem::path workspace_path;
    std::filesystem::path tool_runtime_executable;
    sandbox::config sandbox_config;
    bool refresh_workspace = false;
    StreamCallback stream;
    EventLogCallback event_log;
};

struct SessionResult
{
    nlohmann::json history;
    provider::RequestUsage usage = provider::UsageState::unavailable;
};

class Session final
{
public:
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) noexcept;
    Session& operator=(Session&&) noexcept;
    ~Session();

    SessionState state() const noexcept;
    bool finished() const noexcept;
};

Session register_session(SessionConfig&& config);
SessionResult close_session(Session&& session);
~~~

register_session kiểm tra input, lưu API key vào credential session và khởi tạo
context checkpoint từ config caller truyền. <code>tool_runtime_executable</code>
phải trỏ tới file đang tồn tại; Sessions thêm quyền
<code>sandbox::read_write(tool_runtime_executable.parent_path())</code> vào
<code>sandbox_config</code> để runtime dùng các artifact cùng thư mục. Nếu
config đã khai báo cùng path là read_only, <code>config.add</code> báo xung đột.
Sessions không resolve provider, endpoint, model, context limit hoặc compaction
prompt từ catalog. Session là move-only.
close_session dọn credential và trả history cùng usage cuối.

Hai timeout trong config dùng milliseconds và nhận `int` ở public boundary:

- `-1`: map sang `std::numeric_limits<std::uint32_t>::max()` ở bên trong;
- số dương: timeout hữu hạn;
- `0` và mọi số nhỏ hơn `-1`: `std::invalid_argument`.

<code>tool_result_timeout_ms</code> là timeout của process tool_runtime do
Sessions khởi chạy qua <code>sandbox::process</code>. Sessions gửi raw tool call
qua stdin, đọc cặp <code>tool_call</code>/<code>result</code> từ stdout. Lỗi
config, launch, timeout, exit code khác 0 hoặc JSON output sai khiến stage ném
exception và Session chuyển sang closed. Đây không phải tool result lỗi do
tool_runtime tạo trong trường hợp process chưa trả kết quả hợp lệ.

`session_timeout_ms` thuộc trực tiếp lifetime của Session. Khi `register_session()`
tạo `SessionData`, Session arm một watchdog thread với timeout đã normalize. Nếu
Session chưa kết thúc trước deadline, watchdog gọi `std::abort()` để kết thúc
process hiện tại. Kiến trúc giả định một Session chạy trong một process riêng,
nên hard timeout này thu hồi luôn Provider/EventPort/tool đang block mà không cần
Provider có cancellation API.

Watchdog chỉ được cancel sau khi Session thực sự kết thúc: `fail_session()` đã
cleanup xong hoặc `close_session()` đã cleanup credential thành công. Nếu caller
chỉ đi tới state `finished` nhưng chưa close Session thì watchdog vẫn còn hiệu lực.

`event_log` là observer đồng bộ của EventPort nội bộ. `run_turn()` gọi callback
ngay sau khi `Read` được event và trước khi parse/build response. Callback nhận
`const event_port::Event&`, không sở hữu Registration và không cần tự gọi
Register/Read/Close. Ví dụ log thẳng:

~~~cpp
config.event_log = [](const event_port::Event& event)
{
    std::cout
        << event.sequence << ' '
        << event.package << ' '
        << event.type << ' '
        << event.data.dump() << '\n';
};
~~~

Callback chạy trong execution path. Nếu callback chậm thì việc đọc event tiếp
theo cũng chậm và backpressure EventPort vẫn được giữ. Nếu callback ném exception,
Sessions vẫn drain/join provider turn hiện tại rồi propagate exception ra caller.

Ba stage public mô tả ba bước nghiệp vụ:

#### RequestStage

~~~cpp
class RequestStage final
{
public:
    RequestStage(const RequestStage&) = delete;
    RequestStage& operator=(const RequestStage&) = delete;
    RequestStage(RequestStage&&) noexcept;
    RequestStage& operator=(RequestStage&&) noexcept;

    bool compact() const noexcept;
    std::uint64_t context_usage() const noexcept;
    std::uint64_t context_limit() const noexcept;
    const std::string& model() const noexcept;
    provider::Provider selected_provider() const noexcept;
};

RequestStage declare_request(Session& session);
void run_request(Session& session, RequestStage&& stage);
~~~

declare_request tính usage hiện tại, so sánh với compact_threshold caller truyền
và tạo request stage.
run_request gọi provider turn, giữ kết quả pending và chuyển Session sang
response.

#### ResponseStage

~~~cpp
class ResponseStage final
{
public:
    ResponseStage(const ResponseStage&) = delete;
    ResponseStage& operator=(const ResponseStage&) = delete;
    ResponseStage(ResponseStage&&) noexcept;
    ResponseStage& operator=(ResponseStage&&) noexcept;

    bool has_tool_calls() const noexcept;
    std::size_t tool_count() const noexcept;
    const provider::RequestUsage& usage() const noexcept;
};

ResponseStage declare_response(Session& session);
void run_response(Session& session, ResponseStage&& stage);
~~~

declare_response đọc assistant pending và công bố response có bao nhiêu tool
call cùng usage. run_response commit assistant vào history, cập nhật usage
checkpoint, phát context usage và chuyển sang finished hoặc tool.

#### ToolStage

~~~cpp
class ToolStage final
{
public:
    ToolStage(const ToolStage&) = delete;
    ToolStage& operator=(const ToolStage&) = delete;
    ToolStage(ToolStage&&) noexcept;
    ToolStage& operator=(ToolStage&&) noexcept;
    ~ToolStage();

    std::string_view call_id() const;
    std::string_view name() const;
    std::string_view arguments() const;
    const nlohmann::json& raw_call() const noexcept;
};

ToolStage declare_tool(Session& session);
void run_tool(Session& session, ToolStage&& stage);
~~~

declare_tool chuẩn bị raw tool call tiếp theo trong assistant;
<code>raw_call()</code> trả chính giá trị đó. run_tool nhận canonical tool call
và tool result do executable tool_runtime trả về, ghi vào history, sau đó:

- chuyển sang tool stage tiếp theo nếu còn tool call;
- chuyển về request nếu cycle đã hoàn tất.

Các stage object là move-only và mang generation/token nội bộ. Stage khai báo
sai state, dùng lại stage cũ hoặc chạy stage không đúng thứ tự sẽ ném
std::logic_error. Driver bên ngoài không tự tạo stage bằng constructor; stage
được tạo qua declare_*.

State transition chuẩn:

~~~text
request -> response -> finished
                  -> tool -> [tool ...] -> request
~~~

Nhánh tool lặp lại cho tới khi assistant không còn tool call. Một driver khác
có thể dừng giữa các stage, chờ user approval, đổi scheduler hoặc lưu trạng
thái riêng rồi tiếp tục bằng stage API.

context_usage mang JSON dạng {"used": ..., "limit": ...}. Các loại summary,
tool và error dùng delta là payload JSON hoặc text theo event.

### Chạy session loop

~~~cpp
LoopResult loop(
    SessionConfig&& config);
~~~

Điều kiện input:

- history phải là array;
- session_current phải là object có messages array không rỗng và không có
  tools;
- tool_definitions phải là array;
- API key, endpoint và model_id không được rỗng;
- context_limit phải lớn hơn 0;
- tool_runtime_executable phải là regular file tồn tại.

loop nhận nguyên SessionConfig, lưu session credential tạm qua secrets, resolve
thành SecureString khi gửi request và dọn credential khi kết thúc. Khi
assistant có tool calls, loop chạy tool cycle qua sandboxed tool_runtime rồi
gửi vòng tiếp theo. Kết quả cuối trả history và usage của final provider
request. CLI <code>sessions_loop</code> kiểm tra thêm workspace_path là directory
và canonicalize nó trước khi tạo SessionConfig.

loop là driver mặc định và reference implementation của state machine. Nó
không phải entrypoint bắt buộc của Session; consumer có thể gọi
register_session, declare_*, run_* và close_session trực tiếp.

Sessions không có policy 80%/95% mặc định. `compact_threshold` là giá trị tuyệt
đối do caller truyền; `declare_request()` expose context_usage/context_limit để
caller tự áp approval/safety policy giữa declare và run.

Usage thật UsageState::unavailable sau request là lỗi runtime vì loop cần usage
để cập nhật checkpoint.

## 8. tool_runtime

<code>tool_runtime</code> là executable CMake, không có header hoặc hàm C++
public. Sessions khởi chạy executable này trong <code>sandbox::process</code>,
đặt working directory bằng workspace, ghi một JSON tool call vào stdin và đọc
một JSON response từ stdout. Runtime dùng working directory của process làm
workspace; quyền filesystem và network do <code>SessionConfig.sandbox_config</code>
quyết định trước khi process bắt đầu.

### Giao thức stdin/stdout

Stdin là một OpenAI function tool call:

~~~json
{
  "id": "call-1",
  "type": "function",
  "function": {
    "name": "read",
    "arguments": "{\"filePath\":\"README.md\"}"
  }
}
~~~

<code>function.arguments</code> cũng có thể là JSON object hoặc array; dạng
string phải parse được thành JSON. Runtime nạp định nghĩa tool từ
<code>tool_runtime_tools/tool_definitions.json</code> cạnh executable, hoặc
đường dẫn trong <code>HOMEGROWPH_TOOL_DEFINITIONS</code>, rồi chuẩn hóa call và
kiểm tra arguments theo schema trước khi dispatch.

Stdout là một JSON object:

~~~json
{
  "tool_call": {
    "id": "call-1",
    "type": "function",
    "function": {"name": "read", "arguments": "{\"filePath\":\"README.md\"}"}
  },
  "result": {
    "role": "tool",
    "tool_call_id": "call-1",
    "content": "<JSON envelope>"
  }
}
~~~

<code>tool_call</code> là canonical call để ghi vào assistant history.
<code>result.content</code> là JSON được serialize thành string; envelope có
<code>version: 1</code>, định nghĩa <code>tool</code>, <code>call_id</code> và
array <code>results</code>. Mỗi item trong <code>results</code> biểu thị
<code>ok</code>, kết quả hoặc lỗi. Input JSON không hợp lệ cũng được chuyển
thành response lỗi với call ID dự phòng.

### Tool được dispatch

Runtime hiện đăng ký:

| Tool      | Worker được gọi       |
| --------- | --------------------- |
| read      | TypeScript/Node       |
| write     | TypeScript/Node       |
| glob      | TypeScript/Node       |
| grep      | TypeScript/Node       |
| webfetch  | TypeScript/Node       |
| todowrite | TypeScript/Node       |
| edit_file | native C++ executable |

Quyền được khai báo cho process tool_runtime ở Sessions/sandbox, không được
chọn riêng theo tên tool trong runtime. Runtime gọi worker Node hoặc
<code>edit_file</code> như child process. Node được tìm cạnh executable, sau
đó qua <code>HOMEGROWPH_NODE</code> hoặc PATH. Có thể override các đường dẫn:

~~~text
HOMEGROWPH_NODE
HOMEGROWPH_TOOL_HOST
HOMEGROWPH_EDIT_FILE
HOMEGROWPH_TOOL_DEFINITIONS
~~~

Các lỗi như <code>invalid_tool_call</code>,
<code>tool_schema_not_found</code>, <code>invalid_tool_call_schema</code>,
<code>invalid_arguments</code>, <code>unsupported_tool</code>,
<code>unsupported_native_tool</code>, <code>tool_process_failed</code>,
<code>invalid_tool_output</code>, <code>invalid_tool_result</code> và
<code>tool_execution_error</code> nằm trong item lỗi của
<code>result.content</code>. Timeout của process tool_runtime do Sessions quản
lý, không phải một mã lỗi do executable này tạo. Header trong
<code>src/tool_runtime</code> và namespace <code>tool_runtime::detail</code>
thuộc implementation nội bộ.

## 9. fsystem

Header:

~~~cpp
#include <fsystem>
~~~

Implementation chọn Windows hoặc Linux theo CMake; kiểu dữ liệu và hàm public
giống nhau trên hai nền tảng.

### File edit

~~~cpp
enum class EditNote
{
    none,
    old_data_not_found,
    old_data_appears_more_than_once,
    file_changed,
    old_data_occurrences_overlap,
    timeout,
};

struct EditRequest
{
    std::filesystem::path path;
    std::string old_content;
    std::string new_content;
};

using EditRequests = std::vector<EditRequest>;

struct EditResult
{
    std::filesystem::path path;
    std::uint32_t error = 0;
    EditNote note{EditNote::none};
    bool replace_attempted = false;
};

using EditResults = std::vector<EditResult>;

EditResults edit(const EditRequests& requests);

EditResult edit(
    const std::filesystem::path& path,
    const std::string& old_data,
    const std::string& new_data);
~~~

edit thay đúng occurrence duy nhất của old_content. Nhiều request có thể được
gửi theo batch. error là mã lỗi OS; note mô tả kết quả nghiệp vụ;
replace_attempted cho biết đã đi tới bước replace cuối hay chưa.

### File watcher

~~~cpp
enum class EventStatus
{
    None,
    HasEvent,
    NoEvent
};

struct WatcherResult
{
    EventStatus event_status = EventStatus::None;
    std::uint32_t error = 0;
};

WatcherResult watcher(
    const std::filesystem::path& path,
    int timeout_f);
~~~

timeout_f là timeout do backend watcher diễn giải. Kết quả phân biệt có event,
hết timeout không có event và lỗi OS.

Executable edit_file là adapter JSON/CLI cho tool runtime; nó không thay đổi
API C++ của fsystem.

## 10. Umbrella C++ target và executable

Target <code>homegrowh_harness::homegrowh_harness</code> là INTERFACE và link
các thư viện C++ core, gồm sandbox và sessions. Nó không link executable
<code>tool_runtime</code> vào ứng dụng. Consumer C++ có thể link:

~~~cmake
add_subdirectory("D:/homegrowh_harness" homegrowh_harness-build)
target_link_libraries(my_cli PRIVATE homegrowh_harness::homegrowh_harness)
~~~

Consumer có thể include <code>&lt;homegrowh_harness&gt;</code> để dùng các
thư viện C++ core hoặc include từng header package khi chỉ cần một phần API.

### Artifact executable

Build tổng tạo các artifact ổn định trong thư mục executable:

~~~text
sessions_loop[.exe]
tool_runtime[.exe]
edit_file[.exe]
tool_runtime_tools/
~~~

CLI session nhận một file JSON:

~~~text
sessions_loop <config.json>
~~~

Các trường bắt buộc trong <code>config.json</code>:

~~~json
{
  "api_key": "<provider key>",
  "provider": "openai",
  "endpoint": "https://example.com/v1/chat/completions",
  "model_id": "<model>",
  "context_limit": 100000,
  "compact_threshold": 80000,
  "session_current": {"messages": [{"role": "user", "content": "Xin chào"}]},
  "tool_definitions": [],
  "workspace_path": "<workspace directory>",
  "tool_runtime_executable": "<path to tool_runtime executable>",
  "compaction_prompt_path": "<path to text file>",
  "sandbox_config": {
    "read_only": [],
    "read_write": ["<workspace directory>"],
    "network": "none"
  }
}
~~~

<code>history</code> là tùy chọn, mặc định array rỗng.
<code>history</code>, <code>session_current</code> và
<code>tool_definitions</code> nhận inline JSON hoặc path tới JSON file.
<code>refresh_workspace</code> mặc định false;
<code>tool_result_timeout_ms</code> và <code>session_timeout_ms</code> mặc định
-1. Trong <code>sandbox_config</code>, hai array path là tùy chọn, còn
<code>network</code> phải là <code>none</code> hoặc
<code>internet_client</code>. CLI đọc <code>api_key</code> từ file JSON; nó
không đọc các biến môi trường <code>HH_API_KEY</code>,
<code>HH_PROVIDER</code> hay <code>HH_ENDPOINT</code>.

Sessions chạy <code>tool_runtime</code> qua API thư viện
<code>sandbox::process</code>. Tool runtime gọi <code>edit_file</code> hoặc
TypeScript tool host khi cần; <code>tool_runtime_tools/</code> chứa các artifact
TypeScript và định nghĩa tool được đóng gói cùng executable.

## 11. TypeScript @hh/tools

Package:

~~~text
package/tools
~~~

Entry export @hh/tools có bảy callable public tool. Mỗi tool có dạng
PublicTool:

~~~ts
type PublicTool = ((
  toolCall: OpenAIFunctionToolCall,
  context: ToolExecutionContext,
) => Promise<OpenAIToolResultMessage>) & {
  readonly definition: OpenAIFunctionToolDefinition;
};
~~~

Exports:

~~~text
read
write
edit_file
glob
grep
webfetch
todowrite
~~~

Contract chung:

~~~ts
interface OpenAIFunctionToolCall {
  id: string;
  type: "function";
  function: {
    name: string;
    arguments: Record<string, unknown> | Array<Record<string, unknown>>;
  };
}

interface ToolExecutionContext {
  repositoryPath: string;
  conversationId?: string;
  requestId?: string;
  signal?: AbortSignal;
}

interface OpenAIToolResultMessage {
  role: "tool";
  tool_call_id: string;
  content: string;
}
~~~

Schema đầy đủ của definition nằm tại
package/tools/src/tool_definitions.json:

| Tool      | Trường chính                          |
| --------- | ------------------------------------- |
| read      | filePath, offset?, limit?             |
| write     | filePath, content                     |
| edit_file | path, old_content, new_content        |
| glob      | pattern, path?                        |
| grep      | pattern, path?, include?              |
| webfetch  | url, format?, timeout?                |
| todowrite | todos[] gồm content, status, priority |

Quyền mà các tool cần để làm việc:

- read, glob, grep: read-only workspace;
- write, edit_file: read-write workspace;
- webfetch: internet_client;
- todowrite: không cần filesystem hoặc network.

Trong đường C++ hiện tại, <code>sandbox_config</code> được áp cho cả process
tool_runtime; runtime không tự đổi quyền sandbox theo từng tên tool.

edit_file đi qua executable/edit_file[.exe]; sáu tool còn lại chạy qua
TypeScript tool host. Đây là facade TypeScript riêng; C++ Sessions gọi
executable tool_runtime qua sandbox::process.

## 12. TypeScript @hh/chat-thread

UI plugin nằm tại `plugins/chat-workspace` và hiện chỉ public bundle browser:

~~~text
@hh/chat-thread/embed
~~~

### Embed

~~~ts
function mountChatThread(
  target: HTMLElement | string,
): () => void;

function unmountChatThread(
  target: HTMLElement | string,
): void;
~~~

mountChatThread mount React app vào element hoặc selector và trả disposer.
unmountChatThread an toàn khi target không tồn tại hoặc chưa mount. Khi bundle
embed được nạp trong browser, custom element <chat-thread> cũng được đăng ký.

Plugin không còn desktop/preload adapter. Tích hợp Session dùng
`@hh/session-client` và process `session_runtime.exe` theo command/event protocol.

## 13. Không thuộc public API

Các thành phần sau không được xem là API consumer:

- provider::SSE, provider::RequestState và các header trong
  package/provider/src/request;
- sessions::detail::*, tool_runtime::detail::* và event_port::detail::*;
- adapter sessions::request trong package/sessions/src/request;
- platform backend trong sandbox, secrets, fsystem;
- code generator provider và generated file path trong build;
- executable protocol nội bộ của edit_file và các header dưới
  package/tool_runtime/src;
- test helpers, benchmark và mọi header dưới tests.

Consumer nên include facade header và link target CMake tương ứng. Những symbol
nội bộ có thể thay đổi khi implementation hoặc platform thay đổi.
