# BLOCK DATABASE

## 1. Trạng thái hiện tại

Database block đang được hiện thực bằng package C++ core/events, không còn package TypeScript @hh/database trong cây package hiện tại.

Public header:

~~~cpp
#include <events>
~~~

Target CMake:

~~~text
events::events
~~~

Block này chỉ sở hữu event store SQLite. Các khái niệm repository, conversation registry, contextUsage và algorithm trong tài liệu cũ không có trong implementation hiện tại.

## 2. Public data model

Input khi ghi:

~~~cpp
struct EventInput {
    std::string events;
    std::optional<std::string> create_at;
    std::string session_id;
    std::string provider;
    std::string model;
};
~~~

Row khi đọc:

~~~cpp
struct Event {
    std::int64_t row_position;
    std::string events;
    std::string create_at;
    std::string session_id;
    std::string provider;
    std::string model;
};
~~~

events là text thô và có thể chứa JSON/SSE event. Database không parse nội dung này thành message.

## 3. Store và schema

~~~cpp
events::Store store(database_path);
events::Store created = events::create(id, directory);
~~~

Store:

- mở hoặc tạo một SQLite database tại database_path;
- là move-only;
- giữ SQLite state bằng ownership nội bộ;
- tuần tự hóa các lời gọi trên cùng Store bằng mutex.

events::create(id, directory) tạo directory/id.db và lỗi nếu file đã tồn tại. id phải là tên file, không phải path.

Schema hiện tại:

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

Không có cột id, sort_order, request_id hoặc event_index trong schema hiện tại.

## 4. Row position

row_position là vị trí nguyên bắt đầu từ 0:

~~~text
0, 1, 2, ... N-1
~~~

Public operations:

~~~cpp
std::int64_t events::append(Store&, const EventInput&);
std::int64_t events::insert_after(Store&, std::int64_t row_position, const EventInput&);
bool events::erase(Store&, std::int64_t row_position);
std::vector<Event> events::query(const Store&);
std::vector<Event> events::query(const Store&, const std::string& session_id);
~~~

Invariant:

- append thêm tại max(row_position) + 1; database rỗng bắt đầu ở 0;
- insert_after(P) chèn tại P + 1 và dịch các row phía sau lên 1;
- erase(P) xóa P và dịch các row phía sau xuống 1;
- query luôn ORDER BY row_position;
- query(store, session_id) lọc Session_ID khớp chính xác rồi vẫn giữ thứ tự row_position.

Các thay đổi vị trí của insert/erase chạy trong SQLite transaction.

## 5. Timestamp và SQLite

Nếu EventInput.create_at có giá trị, Database lưu nguyên văn.

Nếu không có, insert dùng timestamp UTC:

~~~text
YYYY-MM-DDTHH:MM:SS.sssZ
~~~

SQLite được mở read/write/create, FULLMUTEX, bật extended result codes và busy timeout 5000 ms.

## 6. Quan hệ với Sessions

Database không tự tạo chat history.

Sessions có hai projection API đọc event database:

~~~cpp
sessions::convert_history(database_path, session_id);
sessions::convert_all_history(database_path);
~~~

Projection đọc events theo row_position, chia segment theo provider và dựng lại OpenAI-compatible messages từ raw provider events cho openai, deepseek và bonsai.

Đây là consumer của event store; nó không làm thay đổi schema hoặc ownership của Database.

## 7. Boundary

Database hiện tại chịu trách nhiệm:

~~~text
SQLite lifetime
event row storage
integer row ordering
append / insert_after / erase / query
session_id + provider + model metadata
~~~

Database hiện tại không chịu trách nhiệm:

~~~text
conversation registry
active conversation
repository registry
context-usage record
algorithm table
model registry
provider routing
tool execution
session orchestration
~~~

> Nguồn sự thật hiện tại của block Database là core/events và schema events, không phải API @hh/database cũ.
