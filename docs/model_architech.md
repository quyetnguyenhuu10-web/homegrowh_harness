Các Khối lớn độc lập:
1. Sessions - nơi điều phối toàn bộ lifecycle request; trực tiếp phối hợp public API của Database / Provider / Compaction / Tools - `D:\homegrowh_harness\package\session`
2. Provider - API gọi provider đúng 1 lần - `D:\homegrowh_harness\package\provider`
3. compaction - sở hữu toàn bộ nghiệp vụ compact: tự quyết định có cần compact hay không; nếu cần thì gọi public API của Provider, compact history và trả history đã xử lý về Session - `D:\homegrowh_harness\package\compaction`
4. Execute tool - chỉ có việc chạy tool - `D:\homegrowh_harness\package\tools`
5. UI - `plugins/chat-workspace`: React/browser plugin; điều khiển core qua `@hh/session-client` bằng command và nhận EventPort event qua session runtime.
6. Database - nơi chứa cơ sở dữ liệu và các API public cho phép nơi khác tương tác lên dữ liệu. - `D:\homegrowh_harness\package\database`

Quy ước runtime:
> Session = toàn bộ khoảng thời gian từ lúc user nhấn gửi đến khi agent/tool loop kết thúc.
> Request = đúng một lần gọi Provider.
> Mọi Request thường đi cùng một runtime pipeline bất kể thuộc Session nào: lấy requestsBeforeCurrent/currentRequest → build Usage Context trong RAM → usage → check compaction → hard-limit check → PASS mới commit DB → Provider.
> Compaction cho Session N chỉ được nhìn Session 1..N-1; Request thường vẫn nhìn compaction boundary + toàn bộ state hiện có của Session N.

Cốt lõi:
Khối lớn chính là đối tượng private
các API public chính là các hàm public
=> Tính đóng gói tương tự class trong C++

# Question1: Vì sao không cho event ra UI mà phải lòng vòng, DB chuyển thông tin row cho UI truy vấn lấy content?
> Vì Renderer rất mong manh, UI thì cẩn phản ánh sự thật. Cho nên chỉ 1 nơi được phép làm sự thật, đó là DB. UI chỉ đóng vai trò render ra sự thật đã tồn tại.
> Thông báo nói cho UI biết nơi nào sự thật đã thay đổi. Cơ sở dữ liệu nói cho UI biết sự thật đó là gì.

# Question2: Vì sao lại chọn tính đóng gói tương tự C++?
> Mỗi module có các ranh giới riêng giúp việc bảo trì code trở nên dễ dàng

# Question3: Tại sao lại chọn các module độc lập + 1 điều phối
> Cách này giúp các module có tính độc lập. Chỉ nơi điều phối mới đc phép dùng public API giúp các giai đoạn chạy(vòng đời hệ thống) app ở chung một chỗ, dễ sửa, dễ bảo trì, dễ đọc, dễ implention.
> Các module không phải điều phối sẽ hoàn toàn không gọi nhau, không biết nhau. Giúp việc chia nhỏ + cục bộ hóa vai trò giúp dễ dàng implention, bảo trì.
> Điểm cốt lõi: Giảm tối đa tính dây truyền, sửa 1 chỗ không làm gãy dây truyền và cục bộ hóa công việc.

Ngoại lệ có chủ đích:
> Compaction được phép gọi public API của Provider vì đây là một phần nội bộ của chính nghiệp vụ compact. Session vẫn là nơi duy nhất điều phối vòng đời hệ thống; Compaction không điều phối các block khác.
