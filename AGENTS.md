1. Khi dùng các API OS luôn thêm if else,try catch hoặc mệnh đề phù hợp để lưu lỗi vào struct <error> (đặt tên theo ngữ cảnh packet). Không được throw trần.
2. Áp dụng RAII cho các tài nguyên nguyên quan trọng như HANDLE và các loại quan trọng.
3. Các thư viện cấp thấp đặt trong `lib/`; `package/` dành cho Session. Cây thư viện tổ chức như sau:
lib/
    ipc/
        src/
            linux/
            windows/
        include/
            ipc
        test/
        CmakeLists.txt
- file trong include chỉ được là 1 file umbrella, các header detail để cùng với C++ tương ứng của nó.
- Bên trong implementation chi tiết chia các domain của library thành các file/folder trách nhiệm
- logic chung để ở src/
4. Không dùng các câu suy diễn lỗi, chỗ nào cần lỗi, lấy lỗi gốc và lưu vào struct
5. Dùng các kiểu dữ liệu biểu diễn semantics. Ví dụ: std::move cho các nơi cần chuyển quyền sở hữu. tránh dùng byvalue trần(để không bị copy không cần thiết)
6. Các libary generic tuyệt đối, không tồn tại bất cứ hardcode nào.