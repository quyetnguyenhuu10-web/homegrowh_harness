# Linux edit stress

Chạy các lệnh dưới đây từ thư mục `package/tools/filesystems`. Có thể truyền thêm `--build-dir <đường-dẫn-tương-đối-hoặc-tuyệt-đối>`; mặc định script tự tìm các thư mục build chuẩn.

## Build & chạy theo profile

Build một lần:

```
cmake -S . -B build -DFILESYSTEMS_BUILD_TESTS=ON -DFILESYSTEMS_BUILD_LINUX_STRESS_TESTS=ON
cmake --build build --target linux_edit_stress_bench --parallel
```

Chạy theo profile (`--build-dir build` để script tìm exe vừa build):

| Profile    | Lệnh                                                                                        | Ghi chú                            |
| ---------- | ------------------------------------------------------------------------------------------- | ---------------------------------- |
| `smoke`    | `node tests/platform/linux/fsystem/edit/linux_edit_stress/run_all.js --profile smoke --build-dir build`       | Mặc định, chạy nhanh               |
| `standard` | `node tests/platform/linux/fsystem/edit/linux_edit_stress/run_all.js --profile standard --build-dir build`   | Như CI (`linux_edit_stress_standard_test`) |
| `extreme`  | `node tests/platform/linux/fsystem/edit/linux_edit_stress/run_all.js --profile extreme --build-dir build`    | Chỉ chạy tay                       |

## Profiles và cỡ file

| Profile    | Cỡ file                                                        |
| ---------- | -------------------------------------------------------------- |
| `smoke`    | 1 MB, 16 MB                                                    |
| `standard` | 1 MB, 4 MB, 16 MB, 32 MB, 64 MB, 128 MB, 256 MB, 512 MB        |
| `extreme`  | 1 GB, 2 GB, 3 GB, 4 GB                                         |

## Ma trận case

Mỗi cỡ file chạy: vị trí pattern (đầu / giữa / cuối) × độ dài thay thế (ngắn 32 B / dài = cỡ file ÷ 16) × chế độ (clean + 4 loại interference: modify, delete, rename, lock). Độ dài marker `old_data` scale theo cỡ file để scatter phân tích có độ trải.

Truyền `--repeat N` (mặc định 1) để mỗi ô ma trận chạy N lần edit — id case thêm hậu tố `-r1…-rN`, mỗi biểu đồ trong size có từng đó lần edit. VD: `run_all.js --profile smoke --repeat 3`.

## Mô hình can thiệp

- `lock`: giữ exclusive lock **trước** khi edit chạy (Linux: advisory `flock`, chỉ đo việc giữ lock + nội dung nguyên vẹn).
- `modify` / `delete` / `rename`: tác động **trong lúc** edit — thread can thiệp bắn phát đầu sau 1 ms rồi lặp liên tục tới khi edit xong (rename đảo qua lại), đảm bảo rơi vào cửa sổ edit.
- Can thiệp rơi ra **sau** khi edit xong thì không được tính là edit bỏ sót (kết quả `inconclusive`, không phải `fail`).
- Mọi tiêu chí (clean + từng loại can thiệp) chạy đủ số lần bằng nhau: case nào can thiệp rơi sau edit sẽ chạy lại tối đa 10 lần; quá 10 lần mới bỏ khỏi kết quả và in `dropped`.
- Lưu ý: có thể gặp case can thiệp rơi ra sau khi edit xong vì edit quá nhanh (xong trước cả vòng lặp can thiệp) — đây **không phải lỗi edit**, mà là case vô nghĩa đối với phân tích nên đã bị loại.

## Hạng mục

Report `report.html` chia tab riêng từng cỡ file (không cuộn dài). Mỗi tab 1 biểu đồ duy nhất + bảng riêng:

| Thành phần             | Nội dung                                                                                        |
| ---------------------- | ----------------------------------------------------------------------------------------------- |
| Summary Interference   | Thẻ `Rejected: R / T · P%` — chỉ tính interference trước/trong edit (sau edit đã loại)          |
| Summary Clean          | Thẻ `Success: P / T · P%` trên clean case                                                        |
| RAM & Time             | Mỗi cột 1 lần edit (`Edit 1…N`): cao = RAM (MB), số trong cột = time (giây); xanh = xong, đỏ = bị từ chối; scroll ngang + zoom đổi khoảng cách cột, trục Y đứng yên |
| Bảng records           | Riêng từng tab: search, lọc outcome, sort cột; nhấn vào cột để ghim tooltip                     |

| Hạng mục                | Cách chạy bằng file JS                                                                                      |
| ----------------------- | ----------------------------------------------------------------------------------------------------------- |
| Benchmark edit          | `node tests/platform/linux/fsystem/edit/linux_edit_stress/bench/bench.js --profile smoke --build-dir build` |
| Phân tích kích thước    | `node tests/platform/linux/fsystem/edit/linux_edit_stress/size_analysis/size_analysis.js --profile smoke`   (gộp `results.jsonl` của bench, không chạy exe riêng; 2 scatter `old_data`/`new_data` size → duration) |

Chạy toàn bộ:

`node tests/platform/linux/fsystem/edit/linux_edit_stress/run_all.js --profile smoke --build-dir build`

## RAM tách đôi (metrics)

- `ram_harness_bytes`: working set ngay trước lúc edit chạy — phần RAM harness giữ để phục vụ đo (test data, buffers).
- `ram_edit_bytes`: độ đẩy đỉnh mới trong lúc case chạy (`peak_after − peak_before`); bằng 0 khi case tái dùng page cũ — là số thật từng case, không gán đỉnh cũ. Biểu đồ hiện giá trị max.

Mỗi executable C++ ghi result schema ổn định vào `bench/results/results.jsonl`, kèm `schema.json` và `summary.json`. File JS chỉ đọc schema đó để tạo `report.html`, nên có thể thay đổi cách trình bày HTML/biểu đồ mà không phải sửa đầu vào kết quả của C++. `size_analysis` không chạy binary mà gộp `results.jsonl` của bench.

## Cấu trúc code report (`common/report/`)

- `utils.js` — format số/byte, đọc field.
- `charts/` — mỗi loại biểu đồ một file (`pie.js`, `scatter.js`, `grouped.js`, …) + `shell.js` (khung + chú thích).
- `styles.css` / `app.js` — CSS và JS giao diện (tabs, theme, tooltip, search/sort/filter, collapse), inline vào `report.html` lúc render nên file report vẫn độc lập, mở trực tiếp được.
- `report.js` — ráp trang (lọc, phân page/tab, bảng).
- `pipeline.js` — chạy exe, đọc `results.jsonl` (`runCategory`, `runAggregate`).
- `report_runtime.js` — shim giữ tương thích cho các file bench `.js`.
