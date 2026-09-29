#include <Windows.h>

#include <fsystem>

#include "fork/edit/window_edit.h"
#include "fork/irr_probes.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// CONFIG
// ============================================================

constexpr int file_count = 40;
constexpr std::uintmax_t file_size = 16ull * 1024ull * 1024ull;

const std::filesystem::path root_dir =
    R"(D:\homegrowh_harness\test\interference_rejection_rate)";

// argv[1] = JSONL output
// Không có argv[1] = chỉ terminal

// ============================================================
// INTERFERENCE TYPE
// ============================================================

enum class Interference
{
    lock,
    modify,
    delete_file,
    rename
};

const char* to_string(Interference type)
{
    switch (type)
    {
        case Interference::lock:
            return "lock";

        case Interference::modify:
            return "modify";

        case Interference::delete_file:
            return "delete";

        case Interference::rename:
            return "rename";
    }

    return "unknown";
}

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
// RESULT
// ============================================================

struct CaseResult
{
    Interference interference;

    int edit_number;

    std::string path;

    std::string note;

    std::string point;

    std::uint32_t error;

    bool replace_attempted;

    bool interference_applied;

    bool reject_edit;

    double time_seconds;
};

// ============================================================
// SUMMARY
// ============================================================

struct InterferenceSummary
{
    Interference interference;

    int scheduled = 0;
    int applied = 0;
    int filtered = 0;

    int rejected = 0;
    int accepted = 0;

    double rejection_rate = 0.0;
};

// ============================================================
// JSON ESCAPE
// ============================================================

std::string json_escape(const std::string& input)
{
    std::string output;

    output.reserve(input.size() + 16);

    for (char c : input)
    {
        switch (c)
        {
            case '\\':
                output += "\\\\";
                break;

            case '"':
                output += "\\\"";
                break;

            case '\n':
                output += "\\n";
                break;

            case '\r':
                output += "\\r";
                break;

            case '\t':
                output += "\\t";
                break;

            default:
                output += c;
                break;
        }
    }

    return output;
}

// ============================================================
// CREATE ONE 16 MiB FILE
// ============================================================

bool create_file(
    const std::filesystem::path& path,
    int index
)
{
    std::ofstream file(
        path,
        std::ios::binary |
        std::ios::trunc
    );

    if (!file.is_open())
    {
        return false;
    }

    const std::string old_marker =
        "EDIT_TARGET_" +
        std::to_string(index);

    const std::size_t marker_size =
        old_marker.size();

    file.write(
        old_marker.data(),
        static_cast<std::streamsize>(marker_size)
    );

    const std::size_t remaining =
        static_cast<std::size_t>(
            file_size - marker_size
        );

    constexpr std::size_t chunk_size =
        1024 * 1024;

    std::string chunk(
        chunk_size,
        'A'
    );

    std::size_t written = 0;

    while (written < remaining)
    {
        const std::size_t current =
            std::min(
                chunk_size,
                remaining - written
            );

        file.write(
            chunk.data(),
            static_cast<std::streamsize>(current)
        );

        if (!file)
        {
            return false;
        }

        written += current;
    }

    file.close();

    return true;
}

// ============================================================
// CREATE 20 FILES
// ============================================================

std::vector<std::filesystem::path> prepare_files()
{
    std::error_code ec;

    std::filesystem::remove_all(
        root_dir,
        ec
    );

    std::filesystem::create_directories(
        root_dir,
        ec
    );

    if (ec)
    {
        throw std::runtime_error(
            "Failed to create benchmark directory"
        );
    }

    std::vector<std::filesystem::path> files;

    files.reserve(file_count);

    for (int i = 0; i < file_count; ++i)
    {
        const auto path =
            root_dir /
            (
                "file_" +
                std::to_string(i + 1) +
                ".txt"
            );

        if (!create_file(path, i))
        {
            throw std::runtime_error(
                "Failed to create: " +
                path.string()
            );
        }

        files.push_back(path);
    }

    return files;
}

// ============================================================
// ============================================================
// SINGLE-CASE REQUESTS
//
// Each (file, interference) pair is one independent single-request
// fork edit. Requests are built inline in run_batch_fork_type().
// ============================================================

// MODIFY 1 BYTE
// ============================================================

bool interfere_modify(
    const std::filesystem::path& path
)
{
    HANDLE handle =
        CreateFileW(
            path.wstring().c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ |
            FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER offset{};

    /*
        Không sửa old_data ở đầu file.

        Chỉ sửa đúng 1 byte ở giữa file.
    */
    offset.QuadPart =
        static_cast<LONGLONG>(
            file_size / 2
        );

    if (!SetFilePointerEx(
            handle,
            offset,
            nullptr,
            FILE_BEGIN))
    {
        CloseHandle(handle);
        return false;
    }

    const char value = 'Z';

    DWORD written = 0;

    const BOOL ok =
        WriteFile(
            handle,
            &value,
            1,
            &written,
            nullptr
        );

    CloseHandle(handle);

    return ok && written == 1;
}

// ============================================================
// DELETE
// ============================================================

bool interfere_delete(
    const std::filesystem::path& path
)
{
    return DeleteFileW(
        path.wstring().c_str()
    ) != FALSE;
}

// ============================================================
// RENAME
// ============================================================

bool interfere_rename(
    const std::filesystem::path& path
)
{
    const auto destination =
        path.parent_path() /
        (
            path.stem().wstring() +
            L".interfered.rename"
        );

    DeleteFileW(
        destination.wstring().c_str()
    );

    return MoveFileExW(
        path.wstring().c_str(),
        destination.wstring().c_str(),
        MOVEFILE_REPLACE_EXISTING
    ) != FALSE;
}

// ============================================================
// APPLY INTERFERENCE
// ============================================================

bool apply_interference(
    Interference type,
    const std::filesystem::path& path
)
{
    switch (type)
    {
        case Interference::modify:
            return interfere_modify(path);

        case Interference::delete_file:
            return interfere_delete(path);

        case Interference::rename:
            return interfere_rename(path);

        default:
            return false;
    }
}

// ============================================================
// OPEN EXCLUSIVE LOCK
// ============================================================

HANDLE acquire_exclusive_lock(
    const std::filesystem::path& path
)
{
    return CreateFileW(
        path.wstring().c_str(),
        GENERIC_READ |
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
}

// ============================================================
// IS EDIT REJECTED?
// ============================================================

bool is_rejected(
    const fsystem::EditResult& result
)
{
    if (result.error != 0)
    {
        return true;
    }

    if (result.note != fsystem::EditNote::none)
    {
        return true;
    }

    if (!result.replace_attempted)
    {
        return true;
    }

    return false;
}

// ============================================================
// WRITE JSONL CASE
// ============================================================

void write_case_jsonl(
    std::ofstream& file,
    const CaseResult& result
)
{
    file
        << "{"
        << "\"type\":\"edit\","
        << "\"interference\":\""
        << to_string(result.interference)
        << "\","
        << "\"point\":\""
        << result.point
        << "\","
        << "\"edit\":"
        << result.edit_number
        << ","
        << "\"path\":\""
        << json_escape(result.path)
        << "\","
        << "\"note\":\""
        << result.note
        << "\","
        << "\"error\":"
        << result.error
        << ","
        << "\"replace_attempted\":"
        << (result.replace_attempted ? "true" : "false")
        << ","
        << "\"interference_applied\":"
        << (result.interference_applied ? "true" : "false")
        << ","
        << "\"reject_edit\":"
        << (result.reject_edit ? "true" : "false")
        << ","
        << "\"time_seconds\":"
        << std::fixed
        << std::setprecision(6)
        << result.time_seconds
        << "}"
        << '\n';
}

// ============================================================
// WRITE SUMMARY JSONL
// ============================================================

void write_summary_jsonl(
    std::ofstream& file,
    const InterferenceSummary& summary
)
{
    file
        << "{"
        << "\"type\":\"summary\","
        << "\"interference\":\""
        << to_string(summary.interference)
        << "\","
        << "\"scheduled\":"
        << summary.scheduled
        << ","
        << "\"applied\":"
        << summary.applied
        << ","
        << "\"filtered\":"
        << summary.filtered
        << ","
        << "\"rejected\":"
        << summary.rejected
        << ","
        << "\"accepted\":"
        << summary.accepted
        << ","
        << "\"rejection_rate_percent\":"
        << std::fixed
        << std::setprecision(2)
        << summary.rejection_rate
        << "}"
        << '\n';
}

// ============================================================
// ============================================================
// DETERMINISTIC COORDINATION VIA FORK PROBES
//
// The fork (fork/edit + fork/watcher, namespace
// fsystem::windows_irr) fires hooks synchronously on the edit
// thread at exact pipeline points:
//
//   RequestStart -> lock is acquired and held here.
//   TempReady    -> modify / delete / rename happens here, then
//                   wait_observed() blocks until the fork watcher
//                   sees the change, before the commit gate runs.
//
// Every (file, interference) pair is one independent single-
// request edit, so applied == scheduled by construction.
// ============================================================

const char* probe_point_for(Interference type) noexcept
{
    switch (type)
    {
        case Interference::lock:
            return "request_start";

        default:
            return "temp_ready";
    }
}

// Runs ONE full batch (all files, one fork edit call) with per-request
// probe coordination. request_index maps 1:1 to files[index], so each
// element is interfered at its own exact pipeline point.
InterferenceSummary run_batch_fork_type(
    Interference type,
    const std::vector<std::filesystem::path>& files,
    std::vector<CaseResult>& output
)
{
    InterferenceSummary summary;

    summary.interference = type;
    summary.scheduled = file_count;

    const char* point_name = probe_point_for(type);

    fsystem::EditRequests requests;
    requests.reserve(static_cast<std::size_t>(file_count));

    for (int i = 0; i < file_count; ++i)
    {
        requests.push_back({
            files[i],
            "EDIT_TARGET_" + std::to_string(i),
            "EDIT_CHANGED" + std::to_string(i)
        });
    }

    std::vector<bool> applied(
        static_cast<std::size_t>(file_count),
        false
    );

    std::vector<HANDLE> held_locks;
    held_locks.reserve(static_cast<std::size_t>(file_count));

    using clock = std::chrono::steady_clock;

    std::vector<clock::time_point> req_start(
        static_cast<std::size_t>(file_count)
    );

    std::vector<double> req_time(
        static_cast<std::size_t>(file_count),
        0.0
    );

    fsystem::windows_irr::IrrProbes probes;

    probes.hook =
        [&](const fsystem::windows_irr::IrrEvent& event)
    {
        using fsystem::windows_irr::IrrPoint;

        const int index = event.request_index;

        // WatcherReady (index -1): nothing to interfere yet.
        if (index < 0 || index >= file_count)
            return;

        const std::size_t slot =
            static_cast<std::size_t>(index);

        if (event.point == IrrPoint::RequestStart)
        {
            req_start[slot] = clock::now();

            if (type != Interference::lock)
                return;

            HANDLE handle =
                acquire_exclusive_lock(files[slot]);

            if (handle == INVALID_HANDLE_VALUE)
                return;

            held_locks.push_back(handle);
            applied[slot] = true;
            return;
        }

        if (event.point == IrrPoint::RequestEnd)
        {
            req_time[slot] =
                std::chrono::duration<double>(
                    clock::now() - req_start[slot]
                ).count();
            return;
        }

        if (type == Interference::lock ||
            event.point != IrrPoint::TempReady)
        {
            return;
        }

        bool ok = false;

        switch (type)
        {
            case Interference::modify:
                ok = interfere_modify(files[slot]);
                break;

            case Interference::delete_file:
                ok = interfere_delete(files[slot]);
                break;

            case Interference::rename:
                ok = interfere_rename(files[slot]);
                break;

            default:
                break;
        }

        if (!ok)
            return;

        applied[slot] = true;

        // Let the fork watcher observe the change before the
        // commit gate runs. Without this wait the commit can win
        // the race and the interference would be missed.
        event.wait_observed(5000);
    };

    const auto batch_start = clock::now();

    fsystem::EditResults results =
        fsystem::windows_irr::edit_file(requests, probes);

    const double batch_time =
        std::chrono::duration<double>(
            clock::now() - batch_start
        ).count();

    for (HANDLE handle : held_locks)
        CloseHandle(handle);

    const int got = static_cast<int>(results.size());

    for (int i = 0; i < file_count; ++i)
    {
        const std::size_t slot =
            static_cast<std::size_t>(i);

        if (!applied[slot] || i >= got)
        {
            ++summary.filtered;
            continue;
        }

        ++summary.applied;

        const bool rejected = is_rejected(results[slot]);

        if (rejected)
            ++summary.rejected;
        else
            ++summary.accepted;

        const double case_time =
            req_time[slot] > 0.0 ? req_time[slot] : batch_time;

        output.push_back({
            type,
            i + 1,
            files[i].string(),
            to_string(results[slot].note),
            point_name,
            results[slot].error,
            results[slot].replace_attempted,
            true,
            rejected,
            case_time
        });
    }

    const int valid =
        summary.rejected +
        summary.accepted;

    if (valid != 0)
    {
        summary.rejection_rate =
            100.0 *
            static_cast<double>(
                summary.rejected
            ) /
            static_cast<double>(
                valid
            );
    }

    return summary;
}

// PRINT SUMMARY
// ============================================================

void print_summary(
    const InterferenceSummary& summary
)
{
    std::cout
        << "\n----------------------------------------\n";

    std::cout
        << "Interference: "
        << to_string(summary.interference)
        << '\n';

    std::cout
        << "Scheduled: "
        << summary.scheduled
        << '\n';

    std::cout
        << "Applied: "
        << summary.applied
        << '\n';

    std::cout
        << "Filtered: "
        << summary.filtered
        << '\n';

    std::cout
        << "Rejected: "
        << summary.rejected
        << '\n';

    std::cout
        << "Accepted: "
        << summary.accepted
        << '\n';

    std::cout
        << "Rejection rate: "
        << std::fixed
        << std::setprecision(2)
        << summary.rejection_rate
        << "%\n";

    std::cout
        << "----------------------------------------\n";
}

// ============================================================
// MAIN
// ============================================================

int main(int argc, char* argv[])
{
    try
    {
        std::string jsonl_path;

        if (argc >= 2)
        {
            jsonl_path =
                argv[1];

            std::cout
                << "JSONL output: "
                << jsonl_path
                << '\n';
        }

        std::vector<CaseResult> all_results;

        std::vector<InterferenceSummary>
            all_summaries;

        const std::vector<Interference>
            interference_types =
            {
                Interference::lock,
                Interference::modify,
                Interference::delete_file,
                Interference::rename
            };

        // ====================================================
        // EACH INTERFERENCE TYPE:
        //
        // 1. Recreate all test files
        // 2. Run ONE fork edit with the full request batch
        //    (per-request probe-point coordination)
        // ====================================================

        for (Interference type :
             interference_types)
        {
            std::cout
                << "\n\n========================================\n";

            std::cout
                << "INTERFERENCE: "
                << to_string(type)
                << '\n';

            std::cout
                << "Creating "
                << file_count
                << " files x "
                << (file_size / (1024 * 1024))
                << " MiB...\n";

            const auto files =
                prepare_files();

            std::cout
                << "Running ONE full fork batch with "
                << file_count
                << " requests (probe-coordinated)...\n";

            const InterferenceSummary summary =
                run_batch_fork_type(
                    type,
                    files,
                    all_results
                );

            all_summaries.push_back(
                summary
            );

            print_summary(summary);
        }

        // ====================================================
        // FINAL CLEAN FILE SET
        // ====================================================

        prepare_files();

        // ====================================================
        // WRITE JSONL ONCE
        // ====================================================

        if (!jsonl_path.empty())
        {
            std::ofstream jsonl_file(
                std::filesystem::path(
                    jsonl_path
                ),
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

            for (const auto& result :
                 all_results)
            {
                write_case_jsonl(
                    jsonl_file,
                    result
                );
            }

            for (const auto& summary :
                 all_summaries)
            {
                write_summary_jsonl(
                    jsonl_file,
                    summary
                );
            }

            jsonl_file.close();

            std::cout
                << "\nJSONL written once at the end.\n";
        }

        // ====================================================
        // FINAL SUMMARY
        // ====================================================

        std::cout
            << "\n\n========================================\n"
            << "INTERFERENCE REJECTION BENCHMARK\n"
            << "========================================\n";

        for (const auto& summary :
             all_summaries)
        {
            std::cout
                << std::left
                << std::setw(10)
                << to_string(
                    summary.interference
                )
                << " applied="
                << std::setw(3)
                << summary.applied
                << " filtered="
                << std::setw(3)
                << summary.filtered
                << " rejected="
                << std::setw(3)
                << summary.rejected
                << " accepted="
                << std::setw(3)
                << summary.accepted
                << " rate="
                << std::fixed
                << std::setprecision(2)
                << summary.rejection_rate
                << "%\n";
        }

        std::cout
            << "========================================\n";

        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr
            << "ERROR: "
            << e.what()
            << '\n';

        return 1;
    }
}