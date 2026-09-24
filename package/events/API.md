# API Events

## Header công khai và target CMake

Nạp giao diện công khai bằng:

~~~cpp
#include <events>
~~~

Target thư viện CMake là events::events. Gói yêu cầu SQLite development files có thể được tìm thấy bằng find_package(SQLite3 REQUIRED). Gói không tạo executable.

## Kiểu dữ liệu

### EventInput

Các trường truyền vào khi append hoặc insert:

| Trường     | Kiểu                       | Ý nghĩa                                                                  |
| ---------- | -------------------------- | ------------------------------------------------------------------------ |
| events     | std::string                | Nội dung event, được lưu dưới dạng text; có thể chứa JSON.               |
| create_at  | std::optional<std::string> | Thời điểm tạo tùy chọn. Nếu bỏ qua, thư viện ghi thời gian UTC hiện tại. |
| session_id | std::string                | ID phiên, lưu trong cột Session_ID.                                      |
| provider   | std::string                | Tên provider.                                                            |
| model      | std::string                | Tên model.                                                               |

Nếu có create_at, giá trị được lưu nguyên văn. Nếu là std::nullopt, SQLite tự tạo timestamp UTC theo định dạng YYYY-MM-DDTHH:MM:SS.sssZ.

### Event

Một hàng dữ liệu do query trả về:

| Trường       | Kiểu         | Ý nghĩa                                               |
| ------------ | ------------ | ----------------------------------------------------- |
| row_position | std::int64_t | Vị trí bắt đầu từ 0 trong danh sách event đã sắp xếp. |
| events       | std::string  | Nội dung event đã lưu.                                |
| create_at    | std::string  | Thời điểm tạo do caller cung cấp hoặc thư viện tạo.   |
| session_id   | std::string  | ID phiên.                                             |
| provider     | std::string  | Tên provider.                                         |
| model        | std::string  | Tên model.                                            |

Các cột trong bảng SQLite là row_position, events, create_at, Session_ID, provider và model.

## Store

### Khởi tạo và quyền sở hữu

~~~cpp
explicit Store(const std::filesystem::path& database_path);
~~~

Mở hoặc tạo database tại database_path, sau đó khởi tạo bảng events và chỉ mục theo session. Nếu thư mục cha chưa tồn tại, thư viện sẽ tạo thư mục đó. Dùng đường dẫn :memory: để tạo database trong bộ nhớ.

Store sở hữu kết nối SQLite. Store không thể sao chép nhưng có thể di chuyển. Gọi API trên Store đã bị move sẽ ném std::logic_error. Các lời gọi qua cùng một Store được tuần tự hóa.

### append

~~~cpp
std::int64_t append(const EventInput& event);
~~~

Thêm event vào cuối danh sách và trả về row_position được gán. Vị trí bắt đầu từ 0 và luôn liền nhau nếu dữ liệu được thao tác qua API này. Thao tác insert chạy trong một SQLite transaction.

### erase

~~~cpp
bool erase(std::int64_t row_position);
~~~

Xóa hàng tại row_position. Các hàng phía sau giảm vị trí đi 1 để danh sách tiếp tục liền nhau. Trả về false nếu không có hàng tại vị trí đó; nếu xóa thành công thì trả về true. Việc xóa và cập nhật lại vị trí chạy trong cùng một SQLite transaction.

API dùng tên erase vì delete là từ khóa của C++.

### query

~~~cpp
std::vector<Event> query() const;
std::vector<Event> query(const std::string& session_id) const;
~~~

Overload không tham số trả về tất cả các hàng, sắp xếp theo row_position. Overload nhận session_id chỉ trả về các hàng có Session_ID khớp chính xác với session_id, cũng theo thứ tự row_position. Nếu không có hàng phù hợp, kết quả là vector rỗng.

### insert_after

~~~cpp
std::int64_t insert_after(
    std::int64_t row_position,
    const EventInput& event);
~~~

Chèn event ngay sau row_position đang tồn tại và trả về vị trí mới, bằng row_position + 1. Vị trí của các hàng phía sau tăng thêm 1. Nếu row_position không tồn tại, API ném std::out_of_range và không thay đổi dữ liệu. Việc chèn và cập nhật vị trí chạy trong cùng một SQLite transaction.

## Ví dụ

~~~cpp
#include <events>

events::Store store("events.sqlite");

events::EventInput input{
    .events = R"({"type":"message"})",
    .session_id = "session-1",
    .provider = "openai",
    .model = "gpt-5.6-sol"
};

const std::int64_t first = store.append(input);
const std::int64_t inserted = store.insert_after(first, input);
const std::vector<events::Event> rows = store.query("session-1");
const bool removed = store.erase(inserted);
~~~

Lỗi SQLite được báo bằng std::runtime_error. Lỗi filesystem khi tạo thư mục database được báo bằng std::filesystem::filesystem_error.