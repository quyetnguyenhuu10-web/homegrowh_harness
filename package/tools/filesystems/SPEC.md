# JSON File Reader — Specification

## Flow

### 1. Nhận path

**Role:** Nhận path của file chứa path file cần xử lý, giúp thay đổi file cần xử lý mà giảm thiểu việc sửa code.

**Implementation:**

* File:
  `[OpenFile.h](.\include\OpenFile.h)`
  `[OpenFile.cpp](.\src\OpenFile.cpp)`

---

### 2. Lấy path file cần xử lý

**Role:** Đọc file cấu hình/path đầu vào để lấy ra path của file thực sự cần xử lý.

**Input:**

* Path của file chứa path.

**Output:**

* Path của file cần xử lý.

**Implementation:**

* File:
  `[TODO](...)`

---

### 3. Mở file cần xử lý

**Role:** Sử dụng path đã lấy được để mở file cần xử lý và kiểm tra kết quả mở file.

**Success:**

* Tiếp tục sang bước kiểm tra schema.

**Failure:**

* Lấy mã lỗi từ OS.
* Dừng quy trình.
* Trả lỗi.

**Implementation:**

* File:
  `[TODO](...)`

---

### 4. Kiểm tra schema

**Condition:** File đã được mở thành công.

**Role:** Kiểm tra nội dung file có đúng schema JSON yêu cầu hay không.

**Success:**

* Tiếp tục đọc và parse dữ liệu.

**Failure:**

* Dừng quy trình.
* Trả lỗi schema.

**Implementation:**

* File:
  `[TODO](...)`

---

### 5. Đọc và parse dữ liệu

**Condition:** Schema hợp lệ.

**Role:** Đọc nội dung file và chuyển dữ liệu thành `nlohmann::json`.

**Output:**

* `nlohmann::json`

**Implementation:**

* File:
  `[TODO](...)`

---

### 6. Trả kết quả

**Role:** Trả dữ liệu đã parse cho module gọi.

**Output:**

* `nlohmann::json`

**Implementation:**

* File:
  `[TODO](...)`

## Dependencies

| Step | Role                    | File                                                                        |
| ---- | ----------------------- | --------------------------------------------------------------------------- |
| 1    | Nhận path               | `[OpenFile.h](.\include\OpenFile.h)` / `[OpenFile.cpp](.\src\OpenFile.cpp)` |
| 2    | Lấy path file cần xử lý | `[TODO](...)`                                                               |
| 3    | Mở file cần xử lý       | `[TODO](...)`                                                               |
| 4    | Kiểm tra schema         | `[TODO](...)`                                                               |
| 5    | Đọc và parse dữ liệu    | `[TODO](...)`                                                               |
| 6    | Trả kết quả             | `[TODO](...)`                                                               |
