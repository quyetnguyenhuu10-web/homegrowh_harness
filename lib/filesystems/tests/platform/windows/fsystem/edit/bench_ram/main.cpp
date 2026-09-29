#include <iostream>
#include <Windows.h>
#include <psapi.h>
#include <fsystem>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <filesystem>
#include <vector>

// ============================================================
// EDIT NOTE -> STRING
// ============================================================

const char* to_string(fsystem::EditNote note)
{
    switch (note)
    {
        case fsystem::EditNote::none:
            return "none";

        case fsystem::EditNote::old_data_not_found:
            return "old_data_not_found";

        case fsystem::EditNote::old_data_appears_more_than_once:
            return "old_data_appears_more_than_once";

        case fsystem::EditNote::file_changed:
            return "file_changed";

        case fsystem::EditNote::old_data_occurrences_overlap:
            return "old_data_occurrences_overlap";

        case fsystem::EditNote::timeout:
            return "timeout";
    }

    return "unknown";
}

// ============================================================
// GET RAM
// ============================================================

SIZE_T get_ram()
{
    PROCESS_MEMORY_COUNTERS pmc{};

    GetProcessMemoryInfo(
        GetCurrentProcess(),
        &pmc,
        sizeof(pmc)
    );

    return pmc.WorkingSetSize;
}

// ============================================================
// RAM -> MB
// ============================================================

double ram_to_mb(SIZE_T ram)
{
    return ram / (1024.0 * 1024.0);
}

// ============================================================
// BENCHMARK RESULT
// ============================================================

struct EditBenchmarkResult
{
    int edit_number;

    std::string path;
    std::string note;

    double ram_before_mb;
    double ram_peak_mb;
    double ram_after_mb;

    double ram_jsonl_mb;

    double time_seconds;
};

// ============================================================
// WRITE EDIT RESULT TO JSONL
// ============================================================

void write_edit_jsonl(
    std::ofstream& file,
    const EditBenchmarkResult& result
)
{
    file
        << "{"
        << "\"type\":\"edit\","
        << "\"edit\":"
        << result.edit_number
        << ","
        << "\"path\":\""
        << result.path
        << "\","
        << "\"note\":\""
        << result.note
        << "\","
        << "\"ram_before_mb\":"
        << std::fixed
        << std::setprecision(6)
        << result.ram_before_mb
        << ","
        << "\"ram_peak_mb\":"
        << result.ram_peak_mb
        << ","
        << "\"ram_after_mb\":"
        << result.ram_after_mb
        << ","
        << "\"ram_jsonl_mb\":"
        << result.ram_jsonl_mb
        << ","
        << "\"time_seconds\":"
        << result.time_seconds
        << "}"
        << '\n';
}

// ============================================================
// WRITE SUMMARY
// ============================================================

void write_summary_jsonl(
    std::ofstream& file,
    int edit_count,
    double total_time
)
{
    file
        << "{"
        << "\"type\":\"summary\","
        << "\"edit_count\":"
        << edit_count
        << ","
        << "\"total_time_seconds\":"
        << std::fixed
        << std::setprecision(6)
        << total_time
        << "}"
        << '\n';
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[])
{
    const auto time_start =
        std::chrono::steady_clock::now();

    // ========================================================
    // SỐ LẦN EDIT
    // ========================================================

    int edit_count = 1000;

    // ========================================================
    // INPUT FILE
    // ========================================================

    const std::string input_path =
        R"(D:\homegrowh_harness\test_64MB.txt)";

    // ========================================================
    // JSONL PATH
    //
    // argv[1] = path JSONL
    // Không có argv[1] = không ghi JSONL
    // ========================================================

    std::string jsonl_path;

    if (argc >= 2)
    {
        jsonl_path = argv[1];

        std::cout
            << "JSONL output: "
            << jsonl_path
            << '\n';
    }

    const bool use_jsonl =
        !jsonl_path.empty();

    // ========================================================
    // RAM BASELINE
    //
    // Đây là RAM của process trước khi tạo bộ nhớ lưu
    // kết quả benchmark cho JSONL.
    // ========================================================

    const SIZE_T ram_before_jsonl_storage =
        get_ram();

    // ========================================================
    // KẾT QUẢ CHỈ ĐƯỢC LƯU TRONG RAM
    //
    // KHÔNG ghi file trong lúc benchmark.
    // ========================================================

    std::vector<EditBenchmarkResult> benchmark_results;

    if (use_jsonl)
    {
        benchmark_results.reserve(edit_count);
    }

    // ========================================================
    // RAM SAU KHI CHUẨN BỊ STORAGE JSONL
    // ========================================================

    const SIZE_T ram_after_jsonl_storage =
        get_ram();

    // ========================================================
    // RAM JSONL BASE
    //
    // Đây là phần RAM tăng thêm do vùng lưu kết quả JSONL.
    // ========================================================

    SIZE_T jsonl_storage_ram =
        0;

    if (use_jsonl &&
        ram_after_jsonl_storage > ram_before_jsonl_storage)
    {
        jsonl_storage_ram =
            ram_after_jsonl_storage -
            ram_before_jsonl_storage;
    }

    // ========================================================
    // POLLING THREAD
    // ========================================================

    std::atomic<bool> polling{true};
    std::atomic<SIZE_T> ram_peak{0};

    std::thread ram_thread([&]()
    {
        while (
            polling.load(
                std::memory_order_relaxed
            )
        )
        {
            SIZE_T current_ram = get_ram();

            SIZE_T current_peak =
                ram_peak.load(
                    std::memory_order_relaxed
                );

            while (
                current_ram > current_peak &&
                !ram_peak.compare_exchange_weak(
                    current_peak,
                    current_ram,
                    std::memory_order_relaxed,
                    std::memory_order_relaxed
                )
            )
            {
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );
        }
    });

    // ========================================================
    // EDIT TỪNG REQUEST RIÊNG
    // ========================================================

    for (int i = 0; i < edit_count; ++i)
    {
        // ----------------------------------------------------
        // TẠO REQUEST RIÊNG
        // ----------------------------------------------------

        fsystem::EditRequests requests;

        if (i % 2 == 0)
        {
            requests.push_back({
                input_path,
                "con cặc",
                "đi chơi"
            });
        }
        else
        {
            requests.push_back({
                input_path,
                "đi chơi",
                "con cặc"
            });
        }

        // ----------------------------------------------------
        // RAM BEFORE RAW
        // ----------------------------------------------------

        SIZE_T ram_before_raw =
            get_ram();

        // ----------------------------------------------------
        // RAM JSONL HIỆN TẠI
        //
        // Phần RAM dành cho dữ liệu benchmark giữ lại để
        // cuối chương trình mới serialize thành JSONL.
        // ----------------------------------------------------

        SIZE_T ram_jsonl =
            jsonl_storage_ram;

        // ----------------------------------------------------
        // RAM BEFORE SAU KHI TRỪ JSONL
        // ----------------------------------------------------

        SIZE_T ram_before =
            ram_before_raw;

        if (ram_before >= ram_jsonl)
        {
            ram_before -= ram_jsonl;
        }

        // ----------------------------------------------------
        // RESET PEAK
        // ----------------------------------------------------

        ram_peak.store(
            ram_before_raw,
            std::memory_order_relaxed
        );

        // ----------------------------------------------------
        // TIME START
        // ----------------------------------------------------

        const auto edit_start =
            std::chrono::steady_clock::now();

        // ----------------------------------------------------
        // EDIT
        // ----------------------------------------------------

        auto results =
            fsystem::edit(requests);

        // ----------------------------------------------------
        // TIME END
        // ----------------------------------------------------

        const auto edit_end =
            std::chrono::steady_clock::now();

        const double time_edit =
            std::chrono::duration<double>(
                edit_end - edit_start
            ).count();

        // ----------------------------------------------------
        // PEAK RAW
        // ----------------------------------------------------

        SIZE_T ram_peak_raw =
            ram_peak.load(
                std::memory_order_relaxed
            );

        // ----------------------------------------------------
        // PEAK SAU KHI TRỪ JSONL
        // ----------------------------------------------------

        SIZE_T ram_during =
            ram_peak_raw;

        if (ram_during >= ram_jsonl)
        {
            ram_during -= ram_jsonl;
        }

        // ----------------------------------------------------
        // RAM AFTER RAW
        // ----------------------------------------------------

        SIZE_T ram_after_raw =
            get_ram();

        // ----------------------------------------------------
        // RAM AFTER SAU KHI TRỪ JSONL
        // ----------------------------------------------------

        SIZE_T ram_after =
            ram_after_raw;

        if (ram_after >= ram_jsonl)
        {
            ram_after -= ram_jsonl;
        }

        // ----------------------------------------------------
        // NOTE
        // ----------------------------------------------------

        const char* note =
            "no_result";

        if (!results.empty())
        {
            note =
                to_string(results[0].note);
        }

        // ----------------------------------------------------
        // LƯU KẾT QUẢ TRONG RAM
        //
        // Chưa ghi JSONL.
        // ----------------------------------------------------

        if (use_jsonl)
        {
            benchmark_results.push_back({
                i + 1,
                input_path,
                note,
                ram_to_mb(ram_before),
                ram_to_mb(ram_during),
                ram_to_mb(ram_after),
                ram_to_mb(ram_jsonl),
                time_edit
            });

            // ------------------------------------------------
            // Cập nhật RAM JSONL storage sau khi thêm result.
            // ------------------------------------------------

            SIZE_T current_ram =
                get_ram();

            if (current_ram >
                ram_before_jsonl_storage)
            {
                jsonl_storage_ram =
                    current_ram -
                    ram_before_jsonl_storage;
            }
        }

        // ----------------------------------------------------
        // OUTPUT TERMINAL
        // ----------------------------------------------------

        std::cout
            << "Edit "
            << i + 1
            << ": "
            << note
            << " - [RAM before edit: "
            << std::fixed
            << std::setprecision(6)
            << ram_to_mb(ram_before)
            << " MB / Peak RAM during edit: "
            << ram_to_mb(ram_during)
            << " MB / RAM after edit: "
            << ram_to_mb(ram_after)
            << " MB / RAM JSONL: "
            << ram_to_mb(ram_jsonl)
            << " MB / Time: "
            << time_edit
            << " s]"
            << '\n';
    }

    // ========================================================
    // DỪNG POLLING
    // ========================================================

    polling.store(
        false,
        std::memory_order_relaxed
    );

    ram_thread.join();

    // ========================================================
    // TỔNG TIME
    // ========================================================

    const auto time_end =
        std::chrono::steady_clock::now();

    const double total_time =
        std::chrono::duration<double>(
            time_end - time_start
        ).count();

    // ========================================================
    // OUTPUT TỔNG TERMINAL
    // ========================================================

    std::cout
        << "========================================\n";

    std::cout
        << "Edit count: "
        << edit_count
        << '\n';

    std::cout
        << "Total time: "
        << total_time
        << " s\n";

    std::cout
        << "========================================\n";

    // ========================================================
    // GHI JSONL DUY NHẤT 1 LẦN Ở CUỐI
    // ========================================================

    if (use_jsonl)
    {
        std::ofstream jsonl_file;

        jsonl_file.open(
            std::filesystem::path(jsonl_path),
            std::ios::out |
            std::ios::trunc
        );

        if (!jsonl_file.is_open())
        {
            std::cerr
                << "Failed to open JSONL output: "
                << jsonl_path
                << '\n';

            return 1;
        }

        // ----------------------------------------------------
        // Ghi toàn bộ edit results
        // ----------------------------------------------------

        for (const auto& result : benchmark_results)
        {
            write_edit_jsonl(
                jsonl_file,
                result
            );
        }

        // ----------------------------------------------------
        // Ghi summary
        // ----------------------------------------------------

        write_summary_jsonl(
            jsonl_file,
            edit_count,
            total_time
        );

        jsonl_file.close();

        std::cout
            << "JSONL written once at the end.\n";
    }

    return 0;
}