# API Reference

Tài liệu này là danh mục API public đang có trong thư mục package.

## 1. Quy ước public

Các header facade là điểm include được hỗ trợ:

| Package          | Header                                               | Namespace chính      | CMake target                         |
| ---------------- | ---------------------------------------------------- | -------------------- | ------------------------------------ |
| Context usage    | <code>&lt;context_usage&gt;</code>                   | context_usage        | context_usage::context_usage         |
| Event port       | <code>&lt;event_port&gt;</code>                      | event_port           | event_port::event_port               |
| Events           | <code>&lt;events&gt;</code>                          | events               | events::events                       |
| Provider         | <code>&lt;provider&gt;</code>                        | provider             | provider                             |
| Permissions      | <code>&lt;permissions&gt;</code>                       | permissions          | permissions                          |
| Sandbox          | <code>&lt;sandbox&gt;</code>                           | sandbox              | sandbox                               |
| Secrets          | <code>&lt;secrets&gt;</code>                         | secrets              | secrets::secrets                     |
| Sessions         | <code>&lt;sessions&gt;</code>                        | sessions             | sessions::sessions                   |
| Tool runtime     | <code>&lt;tool_runtime&gt;</code>                    | tool_runtime         | tool_runtime::tool_runtime           |
| Filesystems      | <code>&lt;fsystem&gt;</code>                         | fsystem              | filesystems                          |
| Toàn bộ C++ core | <code>&lt;homegrowh_harness&gt;</code>              | các namespace ở trên | homegrowh_harness::homegrowh_harness |

Ví dụ link CLI C++ bên ngoài:

~~~cpp
#include <homegrowh_harness>
~~~

Target umbrella là INTERFACE; nó mang theo include directory và các target
package. Dependency bên thứ ba được CMake gốc quản lý trong cùng build tree.

Các header nằm trong src, detail, platform, tests hoặc codegen không được xem
là API ổn định, dù một số include directory được expose để package khác biên
dịch. Consumer không nên include trực tiếp các header đó.

Các API dùng nlohmann::json cần include dependency JSON thông qua target CMake;
không copy generated header vào source tree.

### Mô hình kiến trúc

Project dùng kiến trúc module theo contract, với tầng điều phối có thể thay
thế. Mỗi package sở hữu một capability và công khai contract qua header facade.
Package khác gọi contract đó; implementation, storage và platform backend nằm
phía sau boundary của package.

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

## 4. events

Header:

~~~cpp
#include <events>
~~~

events là SQLite event store cross-platform, không có executable riêng.

### Mô hình dữ liệu

~~~cpp
struct EventInput
{
    std::string events;
    std::optional<std::string> create_at;
    std::string session_id;
    std::string provider;
    std::string model;
};

struct Event
{
    std::int64_t row_position = 0;
    std::string events;
    std::string create_at;
    std::string session_id;
    std::string provider;
    std::string model;
};
~~~

Schema SQLite:

~~~sql
CREATE TABLE IF NOT EXISTS events (
    row_position INTEGER PRIMARY KEY,
    events TEXT NOT NULL,
    create_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    Session_ID TEXT NOT NULL,
    provider TEXT NOT NULL,
    model TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS events_session_position
ON events(Session_ID, row_position);
~~~

session_id trong C++ ánh xạ tới cột Session_ID. events được lưu dưới dạng
text nguyên bản; store không tự parse thành message.

### Store

~~~cpp
class Store
{
public:
    explicit Store(const std::filesystem::path& database_path);
    ~Store();

    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
};
~~~

Constructor mở hoặc tạo SQLite database, tạo thư mục cha khi cần và khởi tạo
schema. :memory: được hỗ trợ. Các lời gọi trên cùng một Store được tuần tự
hóa. Store đã move thì không còn dùng được.

### Tạo database theo id

~~~cpp
Store create(
    const std::string& id,
    const std::filesystem::path& path);
~~~

Tạo path/id.db và trả về Store đang mở. id phải là tên file đơn, không được
rỗng và không được chứa path. File đã tồn tại gây
std::filesystem::filesystem_error.

### Ghi, xóa và đọc row

~~~cpp
std::int64_t append(Store& store, const EventInput& event);

bool erase(Store& store, std::int64_t row_position);

std::vector<Event> query(const Store& store);

std::vector<Event> query(
    const Store& store,
    const std::string& session_id);

std::int64_t insert_after(
    Store& store,
    std::int64_t row_position,
    const EventInput& event);
~~~

Invariant của row_position:

- database rỗng bắt đầu ở 0;
- append thêm sau row cuối;
- insert_after(P) chèn tại P + 1 và dịch các row phía sau lên một;
- erase(P) xóa P và dịch các row phía sau xuống một;
- query luôn trả theo thứ tự row_position;
- overload có session_id lọc chính xác theo Session_ID;
- insert và erase chạy trong transaction;
- insert_after ném std::out_of_range nếu P không tồn tại;
- overflow vị trí ném std::overflow_error;
- gọi trên Store đã move ném std::logic_error.

Nếu EventInput::create_at là std::nullopt, SQLite tạo timestamp UTC
YYYY-MM-DDTHH:MM:SS.sssZ. Lỗi SQLite được báo bằng std::runtime_error.

## 5. provider

Header:

~~~cpp
#include <provider>
~~~

provider cung cấp kiểu provider, parser usage được generate từ
core/provider/src/request/provider_types.json, HTTP/SSE request, stream
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

## 6. permissions và sandbox

Headers:

~~~cpp
#include <permissions>
#include <sandbox>
~~~

Hai package có boundary tách biệt:

- `permissions` chỉ tạo/resolve permission packet;
- `sandbox` chỉ tạo worker process và quản lý execution tree;
- caller quyết định dùng packet để spawn Node/native process hay workload khác
  bên trong body của `sandbox::run(...)`.

### Filesystem packet

~~~cpp
enum class access
{
    read_only,
    read_write,
};

struct filesystem_request
{
    std::filesystem::path path;
    access mode = access::read_only;
};

struct filesystem_permission
{
    std::filesystem::path path;
    access mode = access::read_only;
    std::wstring capability_name;
    std::wstring sid;
    bool reused = false;
};

struct path_error
{
    std::filesystem::path path;
    std::error_code error;
};

struct filesystem_result
{
    std::vector<filesystem_permission> permissions;
    std::error_code final_error;
    std::vector<path_error> path_errors;
};

using filesystem_packet = filesystem_result;

struct release_result
{
    std::error_code final_error;
    std::vector<path_error> path_errors;
};
~~~

### Filesystem API

~~~cpp
filesystem_packet filesystem(
    const std::vector<filesystem_request>& requests);

release_result release(const std::filesystem::path& path);

release_result release_all();
~~~

Profile/body chọn mode refresh/reuse; Sessions không mang cờ này. `refresh=true`
reconcile capability, còn `refresh=false` reuse durable capability hiện có. Lỗi
theo path nằm trong path_errors; lỗi toàn operation nằm trong final_error.

release gỡ mọi capability của một canonical path. release_all gỡ toàn bộ
filesystem capability do permissions quản lý. Trên Windows chỉ ACE có SID đúng
với capability đã lưu mới bị revoke. Trên Linux release xóa durable state tương
ứng, không mutate host ACL.

### Network packet

~~~cpp
struct network_config
{
    std::string name;
    std::string windows_sid;
};

struct network_permission
{
    std::string name;
    std::wstring sid;
};

using network_packet = network_permission;

network_packet network(const network_config& config);
~~~

`network()` không giữ global process registry. Nó validate cấu hình platform và
trả packet cho caller sử dụng khi tạo process.

### Sandbox boundary

~~~cpp
const int exit_code = sandbox::run(R"(
    # raw PowerShell body on Windows
    # raw shell body on Linux
)");
~~~

`sandbox` không biết filesystem/network permission. Trên Windows nó cung cấp
một PowerShell process nằm trong Job Object để descendants tự thuộc cùng
execution tree; executable PowerShell được resolve bằng `where powershell`,
không hardcode path. Trên Linux body chạy qua `sh` trong process group tương
ứng. Package `permissions` không sở hữu process lifecycle, broker hay process
protocol.

## 7. secrets

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

## 8. sessions

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
    int session_timeout_ms = -1;
    std::string compaction_prompt;
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
context checkpoint từ config caller truyền. Sessions không resolve provider,
endpoint, model, context limit hoặc compaction prompt từ catalog. Session là
move-only.
close_session dọn credential và trả history cùng usage cuối.

Session timeout trong config dùng milliseconds và nhận `int` ở public boundary:

- `-1`: map sang `std::numeric_limits<std::uint32_t>::max()` ở bên trong;
- số dương: timeout hữu hạn;
- `0` và mọi số nhỏ hơn `-1`: `std::invalid_argument`.

Tool-result timeout không thuộc SessionConfig. Profile/body sở hữu timeout này và
truyền xuống runtime/process adapter mà nó spawn.

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
    const nlohmann::json& canonical_call() const;
};

ToolStage declare_tool(Session& session);
void run_tool(Session& session, ToolStage&& stage);
~~~

declare_tool chuẩn bị tool call tiếp theo trong assistant. run_tool thực thi
tool, ghi canonical tool call và tool result, sau đó:

- chuyển sang tool stage tiếp theo nếu còn tool call;
- chuyển về request nếu cycle đã hoàn tất.

Các stage object là move-only và mang generation/token nội bộ. Stage khai báo
sai state, dùng lại stage cũ hoặc chạy stage không đúng thứ tự sẽ ném
std::logic_error. Driver bên ngoài không tự tạo stage bằng constructor; stage
được tạo qua declare_*.

State transition chuẩn:

~~~text
request
  -> response
  -> finished
  -> tool
  -> tool
  -> request
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

loop nhận nguyên SessionConfig, lưu session credential tạm qua secrets, resolve
thành SecureString khi gửi request và dọn credential khi kết thúc. Khi
assistant có tool calls, loop chạy tool cycle rồi gửi vòng tiếp theo. Kết quả
cuối trả history và usage của final provider request.

loop là driver mặc định và reference implementation của state machine. Nó
không phải entrypoint bắt buộc của Session; consumer có thể gọi
register_session, declare_*, run_* và close_session trực tiếp.

Sessions không có policy 80%/95% mặc định. `compact_threshold` là giá trị tuyệt
đối do caller truyền; `declare_request()` expose context_usage/context_limit để
caller tự áp approval/safety policy giữa declare và run.

Usage thật UsageState::unavailable sau request là lỗi runtime vì loop cần usage
để cập nhật checkpoint.

### API request/compaction mức thấp

~~~cpp
provider::CompactionResult request(
    const std::string& api_key_signature,
    provider::Provider selected_provider,
    const std::string& endpoint,
    const std::string& model_id,
    std::string_view compaction_prompt,
    const nlohmann::json& session_current,
    const nlohmann::json& tool_definitions,
    bool compact,
    const nlohmann::json& history = {},
    provider::EventSink response_sink = {},
    provider::EventSink summary_sink = {},
    provider::CompactionResponse* compaction_response = nullptr);
~~~

API này nhận signature đã lưu trong Secrets, resolve bằng
resolve_secure_session, sau đó gọi provider::compaction. Nó không nhận raw
API key. Đây là adapter request mức thấp; nó không thay thế Session kernel và
không điều phối ba stage.

### Chuyển event history thành message history

~~~cpp
nlohmann::json convert_history(
    const std::filesystem::path& database_path,
    const std::string& session_id);

nlohmann::json convert_all_history(
    const std::filesystem::path& database_path);
~~~

Hai hàm đọc database của events, parse raw provider event theo row_position,
hỗ trợ openai, deepseek, bonsai và dựng message array.

- convert_history trả trực tiếp message array cho một session;
- convert_all_history trả array các object
  {"session_id": "...", "history": [...]};
- database path rỗng/không tồn tại hoặc session id rỗng gây exception;
- event JSON không đúng protocol provider gây std::runtime_error.

## 9. tool_runtime

Header:

~~~cpp
#include <tool_runtime>
~~~

### API duy nhất

~~~cpp
nlohmann::json execute(
    const nlohmann::json& tool_call,
    std::vector<std::string>& read_files,
    std::uint32_t timeout_ms);
~~~

tool_call là OpenAI function call:

~~~json
{
  "id": "call-1",
  "type": "function",
  "function": {
    "name": "read",
    "arguments": {"filePath": "README.md"}
  }
}
~~~

arguments cũng có thể là JSON string chứa object hoặc array object.
Workspace là current working directory của process tool_runtime; profile/body
chịu trách nhiệm chọn working directory trước khi spawn process.

Runtime hiện đăng ký:

| Tool      | Runtime               | Filesystem | Network         |
| --------- | --------------------- | ---------- | --------------- |
| read      | TypeScript/Node       | read-only  | none            |
| write     | TypeScript/Node       | read-write | none            |
| glob      | TypeScript/Node       | read-only  | none            |
| grep      | TypeScript/Node       | read-only  | none            |
| webfetch  | TypeScript/Node       | none       | internet client |
| todowrite | TypeScript/Node       | none       | none            |
| edit_file | native C++ executable | read-write | none            |

Kết quả là tool message:

~~~json
{
  "role": "tool",
  "tool_call_id": "call-1",
  "content": "<JSON payload>"
}
~~~

Lỗi dispatch/runtime được trả trong payload với các code như
unsupported_tool, invalid_arguments, process_timeout, invalid_tool_output hoặc
tool_execution_error. Input
tool call không hợp lệ ở mức cấu trúc có thể ném std::invalid_argument.

Runtime mặc định dùng Node tìm từ PATH, TypeScript tool host được build từ
core/tools, còn executable native lấy từ artifact executable. Có thể
override bằng:

~~~text
HOMEGROWPH_NODE
HOMEGROWPH_TOOL_HOST
HOMEGROWPH_EDIT_FILE
~~~

Timeout mặc định của một tool process là 120000 ms. Header
tool_runtime/runtime/runtime.h và namespace tool_runtime::detail là
implementation detail, không phải API consumer.

## 10. fsystem

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

## 11. Umbrella C++ API

Header:

~~~cpp
#include <homegrowh_harness>
~~~

Header này include:

~~~text
context_usage
event_port
events
fsystem
permissions
provider
sandbox
secrets
sessions
tool_runtime
~~~

Consumer C++ bên ngoài chỉ cần link:

~~~cmake
add_subdirectory("D:/homegrowh_harness" homegrowh_harness-build)
target_link_libraries(my_cli PRIVATE homegrowh_harness::homegrowh_harness)
~~~

### Artifact executable

Build tổng tạo các artifact ổn định trong thư mục executable:

~~~text
sessions_loop[.exe]
edit_file[.exe]
~~~

sessions_loop nhận một trong hai dạng:

~~~text
sessions_loop <session_current> <id>
sessions_loop <history> <session_current> <id>
~~~

Mỗi đối số JSON có thể là inline JSON hoặc path tới file JSON. Environment
chính của CLI:

~~~text
HH_API_KEY
HH_PROVIDER
HH_ENDPOINT
HH_CONTEXT_LIMIT
HH_COMPACT_THRESHOLD
PROVIDER_COMPACTION_PROMPT
HH_REFRESH_WORKSPACE
TOOLS_DEFINITIONS
~~~

`sandbox` không còn helper executable riêng; consumer truyền raw body trực
tiếp vào `sandbox::run(...)`. `edit_file` vẫn là native tool executable riêng.

## 12. TypeScript @hh/tools

Package:

~~~text
core/tools
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
core/tools/src/tool_definitions.json:

| Tool      | Trường chính                          |
| --------- | ------------------------------------- |
| read      | filePath, offset?, limit?             |
| write     | filePath, content                     |
| edit_file | path, old_content, new_content        |
| glob      | pattern, path?                        |
| grep      | pattern, path?, include?              |
| webfetch  | url, format?, timeout?                |
| todowrite | todos[] gồm content, status, priority |

Filesystem/network policy:

- read, glob, grep: read-only workspace;
- write, edit_file: read-write workspace;
- webfetch: network internet_client, không cấp filesystem workspace;
- todowrite: không cấp filesystem và network.

edit_file đi qua executable/edit_file[.exe]; sáu tool còn lại chạy qua
TypeScript tool host. Đây là facade TypeScript riêng, còn C++ Sessions gọi
tool_runtime::execute.

## 13. TypeScript @hh/chat-thread

core/chat_thread/package.json khai báo ba entrypoint:

~~~text
@hh/chat-thread/embed
@hh/chat-thread/desktop
@hh/chat-thread/preload
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

### Desktop adapter

~~~ts
interface RegisterChatThreadDesktopOptions {
  dialogTitle?: string;
  dialogButtonLabel?: string;
}

interface ChatThreadDesktopInstallation {
  preloadPath: string;
  dispose(): void;
}

function registerChatThreadDesktop(
  options?: RegisterChatThreadDesktopOptions,
): () => void;

function getChatThreadPreloadPath(): string;

function installChatThreadDesktop(
  options?: RegisterChatThreadDesktopOptions,
): ChatThreadDesktopInstallation;
~~~

Preload expose bridge window.__homegrowhChatThreadDesktop với các nhóm method:

~~~text
model registry:
  listModelRegistry
  addCustomModel
  deleteCustomModel
  getCustomModel
  updateCustomModel
  getSelectedModel
  setSelectedModel

history:
  listConversations
  listRepositories
  addRepository
  addConversation
  deleteConversation
  readConversation
  readConversationRow
  readConversationContextUsage
  setActiveConversation
  getActiveConversation

session:
  sendChatRequest
  cancelChatSession
  listActiveChatSessions

events:
  onChatSessionState
  onProviderErrorNotice
  onConversationContextUsageUpdated
  onCompactionDebug
  onConversationRow
~~~

Các method event trả về hàm unsubscribe.

### Trạng thái tích hợp

chat_thread hiện là UI package riêng và không được add vào root CMake. Desktop
adapter trong source hiện tại vẫn import @hh/database, @hh/provider,
@hh/session kiểu TypeScript cũ. Vì các backend đó không còn nằm trong graph
C++ hiện tại, các export ChatThread được ghi nhận như API source hiện có,
chưa phải adapter đã nối tới sessions_loop C++.

## 14. Không thuộc public API

Các thành phần sau không được xem là API consumer:

- provider::SSE, provider::RequestState và các header trong
  core/provider/src/request;
- sessions::detail::*, tool_runtime::detail::* và event_port::detail::*;
- repository/SQLite statement implementation của events;
- platform backend trong permissions, sandbox, secrets, fsystem;
- code generator provider và generated file path trong build;
- executable protocol/header nội bộ của edit_file;
- test helpers, benchmark và mọi header dưới tests.

Consumer nên include facade header và link target CMake tương ứng. Những symbol
nội bộ có thể thay đổi khi implementation hoặc platform thay đổi.
