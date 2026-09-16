#ifndef _WIN32
#error "windows_edit_stress is a Windows-only stress harness"
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <fsystem>

#include <Windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "Psapi.lib")

namespace
{
    using clock_type = std::chrono::steady_clock;

    constexpr std::uint64_t kibibyte = 1024;
    constexpr std::uint64_t mebibyte = 1024 * kibibyte;
    constexpr std::uint64_t gibibyte = 1024 * mebibyte;
    constexpr std::size_t io_buffer_capacity = 1 * 1024 * 1024;
    constexpr DWORD lock_hold_milliseconds = 5000;
    constexpr std::uint64_t fnv_offset_basis =
        14695981039346656037ull;
    constexpr std::uint64_t fnv_prime = 1099511628211ull;

    enum class interference_action
    {
        none,
        modify,
        delete_file,
        rename_file,
        lock,
        unrelated,
    };

    struct options
    {
        std::string profile = "smoke";
        std::filesystem::path root;
        std::filesystem::path output;
        std::uint64_t large_size = 64 * mebibyte;
        std::size_t short_count = 20;
        std::size_t long_count = 3;
        std::uint64_t seed = 0;
        std::uint32_t interference_wait_ms = 120000;
        bool keep_files = false;
    };

    struct memory_snapshot
    {
        std::uint64_t working_set_bytes = 0;
        std::uint64_t peak_working_set_bytes = 0;
    };

    struct fingerprint
    {
        bool ok = false;
        std::uint64_t size = 0;
        std::uint64_t hash = 0;
        std::uint32_t error = 0;
    };

    struct case_spec
    {
        std::string id;
        std::string scenario;
        std::filesystem::path path;
        interference_action action = interference_action::none;
        std::string expected;
        std::uint64_t original_size = 0;
        std::uint64_t marker_offset = 0;
        std::uint64_t interference_offset = 0;
        std::string old_data;
        std::string new_data;
        bool marker_present = true;
    };

    struct interference_result
    {
        std::uint64_t attempts = 0;
        std::uint32_t first_error = 0;
        bool succeeded = false;
        clock_type::time_point success_time{};
        std::filesystem::path renamed_path;
    };

    struct case_result
    {
        std::string id;
        std::string scenario;
        std::string action;
        std::string expected;
        std::string actual_note;
        std::string outcome;
        std::string detail;
        std::uint64_t file_size_bytes = 0;
        std::uint64_t old_size_bytes = 0;
        std::uint64_t new_size_bytes = 0;
        std::uint64_t duration_ms = 0;
        std::uint64_t working_set_bytes = 0;
        std::uint64_t peak_working_set_bytes = 0;
        std::uint64_t disk_free_before_bytes = 0;
        std::uint64_t disk_free_after_bytes = 0;
        std::uint64_t interference_attempts = 0;
        std::uint32_t error_code = 0;
        std::uint32_t interference_first_error = 0;
        bool interference_succeeded = false;
        bool interference_in_window = false;
        bool lock_at_replace = false;
        bool content_ok = false;
    };

    struct run_totals
    {
        std::size_t total = 0;
        std::size_t passed = 0;
        std::size_t failed = 0;
        std::size_t inconclusive = 0;
    };

    struct handle_deleter
    {
        using pointer = HANDLE;

        void operator()(pointer handle) const noexcept
        {
            if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
                return;

            (void)CloseHandle(handle);
        }
    };

    using unique_handle = std::unique_ptr<void, handle_deleter>;

    std::string action_name(interference_action action)
    {
        switch (action)
        {
        case interference_action::none:
            return "none";
        case interference_action::modify:
            return "modify";
        case interference_action::delete_file:
            return "delete";
        case interference_action::rename_file:
            return "rename";
        case interference_action::lock:
            return "lock";
        case interference_action::unrelated:
            return "unrelated";
        }

        return "unknown";
    }

    std::string note_name(fsystem::EditNote note)
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
        }

        return "unknown";
    }

    std::uint64_t parse_uint64(
        std::string_view value,
        std::string_view option_name
    )
    {
        if (value.empty())
            throw std::runtime_error(
                std::string(option_name) + " requires a value"
            );

        std::size_t parsed = 0;
        const std::string value_string(value);
        const std::uint64_t result = std::stoull(
            value_string,
            &parsed,
            10
        );

        if (parsed != value_string.size())
            throw std::runtime_error(
                std::string(option_name) + " is not an integer"
            );

        return result;
    }

    void apply_profile(options& value)
    {
        if (value.profile == "smoke")
        {
            value.large_size = 64 * mebibyte;
            value.short_count = 20;
            value.long_count = 3;
            return;
        }

        if (value.profile == "standard")
        {
            value.large_size = 512 * mebibyte;
            value.short_count = 100;
            value.long_count = 20;
            return;
        }

        if (value.profile == "extreme")
        {
            value.large_size = 4 * gibibyte;
            value.short_count = 500;
            value.long_count = 50;
            return;
        }

        throw std::runtime_error(
            "profile must be smoke, standard, or extreme"
        );
    }

    void print_usage()
    {
        std::cout
            << "windows_edit_stress options:\n"
            << "  --profile smoke|standard|extreme\n"
            << "  --root <directory>\n"
            << "  --output <directory>\n"
            << "  --large-size-gb <integer>\n"
            << "  --short-count <integer>\n"
            << "  --long-count <integer>\n"
            << "  --seed <integer>\n"
            << "  --interference-wait-ms <integer>\n"
            << "  --keep-files\n";
    }

    options parse_options(int argc, char** argv)
    {
        options result;

        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);

            if (argument == "--help" || argument == "-h")
            {
                print_usage();
                std::exit(0);
            }

            if (argument == "--profile")
            {
                if (index + 1 >= argc)
                    throw std::runtime_error(
                        "--profile requires a value"
                    );

                result.profile = argv[++index];
            }
        }

        apply_profile(result);

        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);

            const auto next_value = [&]() -> std::string_view
            {
                if (index + 1 >= argc)
                    throw std::runtime_error(
                        std::string(argument) + " requires a value"
                    );

                return argv[++index];
            };

            if (argument == "--profile")
            {
                ++index;
            }
            else if (argument == "--root")
            {
                result.root = std::filesystem::path(next_value());
            }
            else if (argument == "--output")
            {
                result.output = std::filesystem::path(next_value());
            }
            else if (argument == "--large-size-gb")
            {
                const std::uint64_t gigabytes = parse_uint64(
                    next_value(),
                    argument
                );

                if (gigabytes >
                    std::numeric_limits<std::uint64_t>::max() /
                        gibibyte)
                {
                    throw std::runtime_error(
                        "--large-size-gb is too large"
                    );
                }

                result.large_size = gigabytes * gibibyte;
            }
            else if (argument == "--short-count")
            {
                result.short_count = static_cast<std::size_t>(
                    parse_uint64(next_value(), argument)
                );
            }
            else if (argument == "--long-count")
            {
                result.long_count = static_cast<std::size_t>(
                    parse_uint64(next_value(), argument)
                );
            }
            else if (argument == "--seed")
            {
                result.seed = parse_uint64(next_value(), argument);
            }
            else if (argument == "--interference-wait-ms")
            {
                result.interference_wait_ms = static_cast<std::uint32_t>(
                    parse_uint64(next_value(), argument)
                );
            }
            else if (argument == "--keep-files")
            {
                result.keep_files = true;
            }
            else if (argument == "--help" || argument == "-h")
            {
                print_usage();
                std::exit(0);
            }
            else
            {
                throw std::runtime_error(
                    "unknown option: " + std::string(argument)
                );
            }
        }

        if (result.seed == 0)
        {
            result.seed = static_cast<std::uint64_t>(
                std::chrono::high_resolution_clock::now()
                    .time_since_epoch()
                    .count()
            );
        }

        if (result.root.empty())
        {
            const auto timestamp =
                std::chrono::system_clock::now()
                    .time_since_epoch()
                    .count();

            result.root = std::filesystem::temp_directory_path() /
                ("fsystem-windows-stress-" +
                 std::to_string(timestamp));
        }

        if (result.output.empty())
            result.output = result.root / "results";

        if (result.large_size < 1 * mebibyte)
            throw std::runtime_error(
                "large size must be at least 1 MiB"
            );

        return result;
    }

    std::uint64_t disk_free_bytes(
        const std::filesystem::path& path
    ) noexcept
    {
        ULARGE_INTEGER free_bytes{};

        if (!GetDiskFreeSpaceExW(
                path.c_str(),
                &free_bytes,
                nullptr,
                nullptr
            ))
        {
            return 0;
        }

        return free_bytes.QuadPart;
    }

    memory_snapshot current_memory() noexcept
    {
        PROCESS_MEMORY_COUNTERS counters{};

        if (!GetProcessMemoryInfo(
                GetCurrentProcess(),
                &counters,
                sizeof(counters)
            ))
        {
            return {};
        }

        return memory_snapshot{
            static_cast<std::uint64_t>(counters.WorkingSetSize),
            static_cast<std::uint64_t>(counters.PeakWorkingSetSize),
        };
    }

    bool write_repeated(
        std::ofstream& output,
        char value,
        std::uint64_t count
    )
    {
        const std::vector<char> buffer(
            io_buffer_capacity,
            value
        );

        while (count != 0)
        {
            const std::uint64_t bytes = std::min<std::uint64_t>(
                count,
                buffer.size()
            );

            output.write(
                buffer.data(),
                static_cast<std::streamsize>(bytes)
            );

            if (!output)
                return false;

            count -= bytes;
        }

        return true;
    }

    bool write_pattern_file(
        const std::filesystem::path& path,
        std::uint64_t file_size,
        std::uint64_t marker_offset,
        std::string_view marker
    )
    {
        if (
            marker_offset > file_size ||
            marker.size() > file_size - marker_offset
        )
        {
            return false;
        }

        std::ofstream output(
            path,
            std::ios::binary |
            std::ios::trunc
        );

        if (!output)
            return false;

        if (!write_repeated(output, 'A', marker_offset))
            return false;

        output.write(
            marker.data(),
            static_cast<std::streamsize>(marker.size())
        );

        if (!output)
            return false;

        if (!write_repeated(
                output,
                'A',
                file_size - marker_offset - marker.size()
            ))
        {
            return false;
        }

        output.flush();
        return output.good();
    }

    void hash_bytes(
        std::uint64_t& hash,
        const char* data,
        std::size_t size
    ) noexcept
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<unsigned char>(data[index]);
            hash *= fnv_prime;
        }
    }

    void hash_repeated(
        std::uint64_t& hash,
        char value,
        std::uint64_t count
    )
    {
        const std::vector<char> buffer(
            io_buffer_capacity,
            value
        );

        while (count != 0)
        {
            const std::uint64_t bytes = std::min<std::uint64_t>(
                count,
                buffer.size()
            );

            hash_bytes(
                hash,
                buffer.data(),
                static_cast<std::size_t>(bytes)
            );

            count -= bytes;
        }
    }

    fingerprint fingerprint_file(
        const std::filesystem::path& path
    ) noexcept
    {
        fingerprint result;

        HANDLE raw_handle = CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ |
            FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_SEQUENTIAL_SCAN,
            nullptr
        );

        if (raw_handle == INVALID_HANDLE_VALUE)
        {
            result.error = GetLastError();
            return result;
        }

        unique_handle handle(raw_handle);
        LARGE_INTEGER file_size{};

        if (!GetFileSizeEx(handle.get(), &file_size) ||
            file_size.QuadPart < 0)
        {
            result.error = GetLastError();
            return result;
        }

        std::vector<char> buffer(io_buffer_capacity);
        std::uint64_t remaining =
            static_cast<std::uint64_t>(file_size.QuadPart);
        std::uint64_t hash = fnv_offset_basis;

        while (remaining != 0)
        {
            const DWORD bytes_to_read = static_cast<DWORD>(
                std::min<std::uint64_t>(
                    remaining,
                    buffer.size()
                )
            );

            DWORD bytes_read = 0;

            if (!ReadFile(
                    handle.get(),
                    buffer.data(),
                    bytes_to_read,
                    &bytes_read,
                    nullptr
                ))
            {
                result.error = GetLastError();
                return result;
            }

            if (bytes_read == 0 || bytes_read > bytes_to_read)
            {
                result.error = ERROR_HANDLE_EOF;
                return result;
            }

            hash_bytes(
                hash,
                buffer.data(),
                bytes_read
            );

            remaining -= bytes_read;
        }

        result.ok = true;
        result.size = static_cast<std::uint64_t>(file_size.QuadPart);
        result.hash = hash;
        return result;
    }

    std::uint64_t expected_hash(
        std::uint64_t file_size,
        std::uint64_t marker_offset,
        std::string_view replacement
    )
    {
        if (
            marker_offset > file_size ||
            replacement.size() > file_size - marker_offset
        )
        {
            return 0;
        }

        std::uint64_t hash = fnv_offset_basis;
        hash_repeated(hash, 'A', marker_offset);
        hash_bytes(hash, replacement.data(), replacement.size());
        hash_repeated(
            hash,
            'A',
            file_size - marker_offset - replacement.size()
        );
        return hash;
    }

    bool matches_expected(
        const std::filesystem::path& path,
        std::uint64_t file_size,
        std::uint64_t marker_offset,
        std::string_view replacement,
        std::uint32_t& error
    )
    {
        const fingerprint actual = fingerprint_file(path);

        if (!actual.ok)
        {
            error = actual.error;
            return false;
        }

        if (
            actual.size != file_size ||
            actual.hash != expected_hash(
                file_size,
                marker_offset,
                replacement
            )
        )
        {
            error = ERROR_INVALID_DATA;
            return false;
        }

        return true;
    }

    std::string make_token(
        std::string_view prefix,
        std::size_t index,
        std::size_t extra_length
    )
    {
        std::string token;
        token.reserve(prefix.size() + 32 + extra_length);
        token.append(prefix);
        token.append("_");
        token.append(std::to_string(index));
        token.append("_");
        token.append(extra_length, 'X');
        return token;
    }

    std::string make_payload(
        std::size_t length,
        std::uint64_t seed
    )
    {
        std::string result(length, 'a');

        for (std::size_t index = 0; index < result.size(); ++index)
        {
            result[index] = static_cast<char>(
                'a' + ((index + seed) % 26)
            );
        }

        return result;
    }

    std::string csv_escape(std::string_view value)
    {
        std::string result;
        result.reserve(value.size() + 2);
        result.push_back('"');

        for (const char character : value)
        {
            if (character == '"')
                result.push_back('"');

            result.push_back(character);
        }

        result.push_back('"');
        return result;
    }

    std::string json_escape(std::string_view value)
    {
        std::string result;

        for (const unsigned char character : value)
        {
            switch (character)
            {
            case '\\':
                result += "\\\\";
                break;
            case '"':
                result += "\\\"";
                break;
            case '\n':
                result += "\\n";
                break;
            case '\r':
                result += "\\r";
                break;
            case '\t':
                result += "\\t";
                break;
            default:
                if (character < 0x20)
                {
                    result += "\\u00";
                    const char* digits = "0123456789abcdef";
                    result.push_back(digits[character >> 4]);
                    result.push_back(digits[character & 0x0f]);
                }
                else
                {
                    result.push_back(static_cast<char>(character));
                }
                break;
            }
        }

        return result;
    }

    class result_logger
    {
    public:
        explicit result_logger(const std::filesystem::path& output)
            : csv_(output / "results.csv"),
              jsonl_(output / "results.jsonl")
        {
            if (!csv_ || !jsonl_)
                throw std::runtime_error(
                    "unable to open result files"
                );

            csv_
                << "id,scenario,action,expected,actual_note,outcome,"
                << "file_size_bytes,old_size_bytes,new_size_bytes,"
                << "duration_ms,working_set_bytes,"
                << "peak_working_set_bytes,disk_free_before_bytes,"
                << "disk_free_after_bytes,interference_attempts,"
                << "interference_first_error,error_code,"
                << "interference_succeeded,"
                << "interference_in_window,lock_at_replace,"
                << "content_ok,detail\n";
        }

        void write(const case_result& result)
        {
            csv_
                << csv_escape(result.id) << ','
                << csv_escape(result.scenario) << ','
                << csv_escape(result.action) << ','
                << csv_escape(result.expected) << ','
                << csv_escape(result.actual_note) << ','
                << csv_escape(result.outcome) << ','
                << result.file_size_bytes << ','
                << result.old_size_bytes << ','
                << result.new_size_bytes << ','
                << result.duration_ms << ','
                << result.working_set_bytes << ','
                << result.peak_working_set_bytes << ','
                << result.disk_free_before_bytes << ','
                << result.disk_free_after_bytes << ','
                << result.interference_attempts << ','
                << result.interference_first_error << ','
                << result.error_code << ','
                << (result.interference_succeeded ? 1 : 0) << ','
                << (result.interference_in_window ? 1 : 0) << ','
                << (result.lock_at_replace ? 1 : 0) << ','
                << (result.content_ok ? 1 : 0) << ','
                << csv_escape(result.detail) << '\n';

            jsonl_
                << "{\"id\":\"" << json_escape(result.id)
                << "\",\"scenario\":\""
                << json_escape(result.scenario)
                << "\",\"action\":\""
                << json_escape(result.action)
                << "\",\"expected\":\""
                << json_escape(result.expected)
                << "\",\"actual_note\":\""
                << json_escape(result.actual_note)
                << "\",\"outcome\":\""
                << json_escape(result.outcome)
                << "\",\"file_size_bytes\":"
                << result.file_size_bytes
                << ",\"old_size_bytes\":"
                << result.old_size_bytes
                << ",\"new_size_bytes\":"
                << result.new_size_bytes
                << ",\"duration_ms\":"
                << result.duration_ms
                << ",\"working_set_bytes\":"
                << result.working_set_bytes
                << ",\"peak_working_set_bytes\":"
                << result.peak_working_set_bytes
                << ",\"disk_free_before_bytes\":"
                << result.disk_free_before_bytes
                << ",\"disk_free_after_bytes\":"
                << result.disk_free_after_bytes
                << ",\"interference_attempts\":"
                << result.interference_attempts
                << ",\"interference_first_error\":"
                << result.interference_first_error
                << ",\"error_code\":"
                << result.error_code
                << ",\"interference_succeeded\":"
                << (result.interference_succeeded ? "true" : "false")
                << ",\"interference_in_window\":"
                << (result.interference_in_window ? "true" : "false")
                << ",\"lock_at_replace\":"
                << (result.lock_at_replace ? "true" : "false")
                << ",\"content_ok\":"
                << (result.content_ok ? "true" : "false")
                << ",\"detail\":\""
                << json_escape(result.detail)
                << "\"}\n";

            csv_.flush();
            jsonl_.flush();
        }

    private:
        std::ofstream csv_;
        std::ofstream jsonl_;
    };

    bool try_modify(
        const case_spec& spec,
        std::uint32_t& error
    )
    {
        HANDLE raw_handle = CreateFileW(
            spec.path.c_str(),
            GENERIC_WRITE,
            FILE_SHARE_READ |
            FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL |
            FILE_FLAG_WRITE_THROUGH,
            nullptr
        );

        if (raw_handle == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            return false;
        }

        unique_handle handle(raw_handle);
        const std::string marker = "INTERFERENCE";
        LARGE_INTEGER offset{};
        offset.QuadPart = static_cast<LONGLONG>(
            spec.interference_offset
        );

        if (!SetFilePointerEx(
                handle.get(),
                offset,
                nullptr,
                FILE_BEGIN
            ))
        {
            error = GetLastError();
            return false;
        }

        DWORD bytes_written = 0;

        if (!WriteFile(
                handle.get(),
                marker.data(),
                static_cast<DWORD>(marker.size()),
                &bytes_written,
                nullptr
            ))
        {
            error = GetLastError();
            return false;
        }

        if (bytes_written != marker.size())
        {
            error = ERROR_WRITE_FAULT;
            return false;
        }

        if (!FlushFileBuffers(handle.get()))
        {
            error = GetLastError();
            return false;
        }

        return true;
    }

    bool try_delete(
        const case_spec& spec,
        std::uint32_t& error
    )
    {
        if (DeleteFileW(spec.path.c_str()))
            return true;

        error = GetLastError();
        return false;
    }

    bool try_rename(
        const case_spec& spec,
        interference_result& result,
        std::uint32_t& error
    )
    {
        result.renamed_path = spec.path;
        result.renamed_path += L".interference-renamed";

        if (MoveFileExW(
                spec.path.c_str(),
                result.renamed_path.c_str(),
                MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_WRITE_THROUGH
            ))
        {
            return true;
        }

        error = GetLastError();
        return false;
    }

    bool try_lock(
        const case_spec& spec,
        unique_handle& lock_handle,
        std::uint32_t& error
    )
    {
        HANDLE raw_handle = CreateFileW(
            spec.path.c_str(),
            GENERIC_READ |
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (raw_handle == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            return false;
        }

        lock_handle.reset(raw_handle);
        return true;
    }

    bool try_unrelated(
        const case_spec& spec,
        std::uint32_t& error
    )
    {
        std::filesystem::path unrelated = spec.path;
        unrelated += L".unrelated";

        std::ofstream output(
            unrelated,
            std::ios::binary |
            std::ios::trunc
        );

        if (!output)
        {
            error = ERROR_ACCESS_DENIED;
            return false;
        }

        output << "unrelated interference\n";
        output.flush();

        if (!output)
        {
            error = ERROR_WRITE_FAULT;
            return false;
        }

        return true;
    }

    void run_interferer(
        const case_spec& spec,
        const options& value,
        std::atomic_bool& stop_requested,
        interference_result& result
    )
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(25)
        );

        const auto deadline =
            clock_type::now() +
            std::chrono::milliseconds(
                value.interference_wait_ms
            );

        unique_handle lock_handle;

        while (
            !stop_requested.load(std::memory_order_acquire) &&
            clock_type::now() < deadline
        )
        {
            ++result.attempts;

            std::uint32_t error = 0;
            bool success = false;

            switch (spec.action)
            {
            case interference_action::modify:
                success = try_modify(spec, error);
                break;
            case interference_action::delete_file:
                success = try_delete(spec, error);
                break;
            case interference_action::rename_file:
                success = try_rename(spec, result, error);
                break;
            case interference_action::lock:
                success = try_lock(spec, lock_handle, error);
                break;
            case interference_action::unrelated:
                success = try_unrelated(spec, error);
                break;
            case interference_action::none:
                return;
            }

            if (success)
            {
                result.succeeded = true;
                result.success_time = clock_type::now();

                if (spec.action == interference_action::lock)
                {
                    Sleep(lock_hold_milliseconds);
                    lock_handle.reset();
                }

                return;
            }

            if (result.first_error == 0)
                result.first_error = error;

            Sleep(2);
        }
    }

    void remove_case_files(
        const case_spec& spec,
        const interference_result& interference
    )
    {
        std::error_code error;
        (void)std::filesystem::remove(spec.path, error);

        if (!interference.renamed_path.empty())
            (void)std::filesystem::remove(
                interference.renamed_path,
                error
            );

        std::filesystem::path unrelated = spec.path;
        unrelated += L".unrelated";
        (void)std::filesystem::remove(unrelated, error);
    }

    case_result execute_case(
        const case_spec& spec,
        const options& value
    )
    {
        case_result result;
        result.id = spec.id;
        result.scenario = spec.scenario;
        result.action = action_name(spec.action);
        result.expected = spec.expected;
        result.file_size_bytes = spec.original_size;
        result.old_size_bytes = spec.old_data.size();
        result.new_size_bytes = spec.new_data.size();
        result.disk_free_before_bytes =
            disk_free_bytes(value.root);

        const auto started = clock_type::now();
        const memory_snapshot memory_before = current_memory();
        interference_result interference;
        std::atomic_bool stop_interference{false};
        std::thread interference_thread;
        bool edit_completed = false;
        clock_type::time_point edit_finished_at = started;

        try
        {
            const std::string_view marker =
                spec.marker_present
                    ? std::string_view(spec.old_data)
                    : std::string_view();

            if (!write_pattern_file(
                    spec.path,
                    spec.original_size,
                    spec.marker_present
                        ? spec.marker_offset
                        : 0,
                    marker
                ))
            {
                result.outcome = "fail";
                result.detail = "unable to create input file";
            }
            else
            {
                if (spec.action != interference_action::none)
                {
                    interference_thread = std::thread(
                        [&]()
                        {
                            run_interferer(
                                spec,
                                value,
                                stop_interference,
                                interference
                            );
                        }
                    );
                }

                const fsystem::EditResult edit_result = fsystem::edit(
                    spec.path,
                    spec.old_data,
                    spec.new_data
                );

                edit_completed = true;
                edit_finished_at = clock_type::now();
                stop_interference.store(
                    true,
                    std::memory_order_release
                );

                if (interference_thread.joinable())
                    interference_thread.join();

                result.error_code = edit_result.error;
                result.actual_note = note_name(edit_result.note);
                result.interference_attempts = interference.attempts;
                result.interference_first_error =
                    interference.first_error;
                result.interference_succeeded = interference.succeeded;
                result.interference_in_window =
                    interference.succeeded &&
                    interference.success_time <= edit_finished_at;
                result.lock_at_replace =
                    spec.action == interference_action::lock &&
                    interference.succeeded &&
                    edit_result.replace_attempted &&
                    edit_result.error == ERROR_SHARING_VIOLATION;

                std::uint32_t verification_error = 0;

                if (spec.expected == "success")
                {
                    const std::uint64_t expected_size =
                        spec.original_size -
                        spec.old_data.size() +
                        spec.new_data.size();

                    result.content_ok = matches_expected(
                        spec.path,
                        expected_size,
                        spec.marker_offset,
                        spec.new_data,
                        verification_error
                    );

                    result.outcome =
                        edit_result.error == 0 &&
                        edit_result.note == fsystem::EditNote::none &&
                        result.content_ok
                            ? "pass"
                            : "fail";

                    if (!result.content_ok)
                    {
                        result.detail =
                            "verification_error=" +
                            std::to_string(verification_error);
                    }
                }
                else if (spec.expected == "not_found")
                {
                    result.content_ok = matches_expected(
                        spec.path,
                        spec.original_size,
                        0,
                        std::string_view(),
                        verification_error
                    );

                    result.outcome =
                        edit_result.error == 0 &&
                        edit_result.note ==
                            fsystem::EditNote::old_data_not_found &&
                        result.content_ok
                            ? "pass"
                            : "fail";
                }
                else if (spec.action == interference_action::unrelated)
                {
                    const std::uint64_t expected_size =
                        spec.original_size -
                        spec.old_data.size() +
                        spec.new_data.size();

                    result.content_ok = matches_expected(
                        spec.path,
                        expected_size,
                        spec.marker_offset,
                        spec.new_data,
                        verification_error
                    );

                    result.outcome =
                        edit_result.error == 0 &&
                        edit_result.note == fsystem::EditNote::none &&
                        result.content_ok
                        ? "pass"
                        : "fail";
                }
                else if (spec.action == interference_action::lock)
                {
                    if (!interference.succeeded)
                    {
                        result.outcome = "inconclusive";
                        result.detail =
                            "exclusive lock was not acquired";
                    }
                    else if (!edit_result.replace_attempted)
                    {
                        result.outcome = "fail";
                        result.detail =
                            "ReplaceFileW was not attempted";
                    }
                    else if (!result.lock_at_replace)
                    {
                        result.outcome = "inconclusive";
                        result.detail =
                            "exclusive lock did not block ReplaceFileW";
                    }
                    else
                    {
                        result.outcome = "pass";
                    }
                }
                else if (
                    !interference.succeeded ||
                    !result.interference_in_window
                )
                {
                    result.outcome = "inconclusive";
                    result.detail =
                        "interference did not land before edit completed";
                }
                else
                {
                    result.outcome =
                        edit_result.error != 0 ||
                        edit_result.note == fsystem::EditNote::file_changed
                            ? "pass"
                            : "fail";
                }
            }
        }
        catch (const std::exception& exception)
        {
            stop_interference.store(
                true,
                std::memory_order_release
            );

            if (interference_thread.joinable())
                interference_thread.join();

            result.outcome = "fail";
            result.detail =
                std::string("exception=") + exception.what();
        }

        if (!edit_completed)
        {
            stop_interference.store(
                true,
                std::memory_order_release
            );

            if (interference_thread.joinable())
                interference_thread.join();
        }

        result.interference_attempts = interference.attempts;
        result.interference_first_error = interference.first_error;
        result.interference_succeeded = interference.succeeded;
        result.disk_free_after_bytes =
            disk_free_bytes(value.root);

        const memory_snapshot memory_after = current_memory();
        result.working_set_bytes = memory_after.working_set_bytes;
        result.peak_working_set_bytes =
            std::max(
                memory_before.peak_working_set_bytes,
                memory_after.peak_working_set_bytes
            );

        result.duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                clock_type::now() - started
            ).count()
        );

        remove_case_files(spec, interference);
        return result;
    }

    std::uint64_t choose_size(
        std::mt19937_64& random,
        std::uint64_t minimum,
        std::uint64_t maximum
    )
    {
        if (maximum <= minimum)
            return minimum;

        std::uniform_int_distribution<std::uint64_t> distribution(
            minimum,
            maximum
        );

        return distribution(random);
    }

    void run_suite(
        const options& value,
        result_logger& logger
    )
    {
        std::mt19937_64 random(value.seed);
        const std::filesystem::path cases_directory =
            value.root / "cases";

        std::filesystem::create_directories(cases_directory);

        run_totals totals;

        const auto run = [&](case_spec spec)
        {
            const case_result result = execute_case(spec, value);
            logger.write(result);

            ++totals.total;

            if (result.outcome == "pass")
                ++totals.passed;
            else if (result.outcome == "fail")
                ++totals.failed;
            else
                ++totals.inconclusive;

            std::cout
                << std::left << std::setw(28) << result.id
                << " " << std::setw(13) << result.outcome
                << " " << std::setw(8) << result.duration_ms
                << " ms error=" << result.error_code
                << " note=" << result.actual_note
                << '\n';
        };

        for (std::size_t index = 0; index < value.short_count; ++index)
        {
            const bool missing = index % 17 == 0;
            const std::size_t old_length =
                3 + (index % 43);
            const std::string old_data = make_token(
                "SHORT_OLD",
                index,
                old_length
            );
            const std::size_t new_length =
                index % 11 == 0
                    ? 0
                    : index % 5 == 0
                        ? 4096 + index
                        : index % 97;
            const std::string new_data = make_payload(
                new_length,
                value.seed + index
            );
            const std::uint64_t file_size =
                256 + (random() % 16384) + old_data.size();
            const std::uint64_t marker_offset =
                random() % (file_size - old_data.size() + 1);

            case_spec spec;
            spec.id = "short-" + std::to_string(index);
            spec.scenario = "short_edit";
            spec.path = cases_directory / (spec.id + ".bin");
            spec.expected = missing ? "not_found" : "success";
            spec.original_size = file_size;
            spec.marker_offset = marker_offset;
            spec.old_data = old_data;
            spec.new_data = new_data;
            spec.marker_present = !missing;
            run(std::move(spec));
        }

        const std::uint64_t maximum_long_size = std::min(
            32 * mebibyte,
            std::max(
                1 * mebibyte,
                value.large_size / 8
            )
        );

        for (std::size_t index = 0; index < value.long_count; ++index)
        {
            const std::string old_data = make_token(
                "LONG_OLD",
                index,
                16 + (index % 31)
            );
            const std::size_t new_length =
                (index % 4 == 0)
                    ? 0
                    : 4096 * (1 + (index % 16));
            const std::string new_data = make_payload(
                new_length,
                value.seed + index + 1000
            );
            const std::uint64_t file_size = choose_size(
                random,
                1 * mebibyte,
                maximum_long_size
            );
            const std::uint64_t marker_offset =
                file_size / 2;

            case_spec spec;
            spec.id = "long-" + std::to_string(index);
            spec.scenario = "long_edit";
            spec.path = cases_directory / (spec.id + ".bin");
            spec.expected = "success";
            spec.original_size = file_size;
            spec.marker_offset = marker_offset;
            spec.old_data = old_data;
            spec.new_data = new_data;
            run(std::move(spec));
        }

        const std::uint64_t large_marker_offset = std::min(
            256 * mebibyte,
            value.large_size / 2
        );

        {
            case_spec spec;
            spec.id = "large-modify-after-progress";
            spec.scenario = "large_edit_interference";
            spec.path = cases_directory / "large-modify.bin";
            spec.action = interference_action::modify;
            spec.expected = "interference";
            spec.original_size = value.large_size;
            spec.marker_offset = large_marker_offset;
            spec.interference_offset = value.large_size / 4;
            spec.old_data = "LARGE_OLD_MARKER";
            spec.new_data = make_payload(8192, value.seed);
            run(std::move(spec));
        }

        const std::uint64_t matrix_size = std::min(
            128 * mebibyte,
            value.large_size
        );
        const std::array<interference_action, 4> actions{
            interference_action::modify,
            interference_action::lock,
            interference_action::delete_file,
            interference_action::rename_file,
        };

        for (std::size_t index = 0; index < actions.size(); ++index)
        {
            case_spec spec;
            spec.id = "interference-" + std::to_string(index);
            spec.scenario = "interference_matrix";
            spec.path = cases_directory / (spec.id + ".bin");
            spec.action = actions[index];
            spec.expected = "interference";
            spec.original_size = matrix_size;
            spec.marker_offset = matrix_size / 2;
            spec.interference_offset = matrix_size / 4;
            spec.old_data = make_token(
                "MATRIX_OLD",
                index,
                32
            );
            spec.new_data = make_payload(
                65536 + index,
                value.seed + index + 4000
            );
            run(std::move(spec));
        }

        {
            case_spec spec;
            spec.id = "interference-unrelated";
            spec.scenario = "interference_matrix";
            spec.path = cases_directory / "interference-unrelated.bin";
            spec.action = interference_action::unrelated;
            spec.expected = "success";
            spec.original_size = matrix_size;
            spec.marker_offset = matrix_size / 2;
            spec.interference_offset = matrix_size / 4;
            spec.old_data = "UNRELATED_OLD_MARKER";
            spec.new_data = make_payload(32768, value.seed + 5000);
            run(std::move(spec));
        }

        std::ofstream summary(value.output / "summary.txt");
        summary << "profile=" << value.profile << '\n'
                << "seed=" << value.seed << '\n'
                << "large_size_bytes=" << value.large_size << '\n'
                << "short_count=" << value.short_count << '\n'
                << "long_count=" << value.long_count << '\n'
                << "total=" << totals.total << '\n'
                << "passed=" << totals.passed << '\n'
                << "failed=" << totals.failed << '\n'
                << "inconclusive=" << totals.inconclusive << '\n';

        std::cout
            << "\nSummary: total=" << totals.total
            << " passed=" << totals.passed
            << " failed=" << totals.failed
            << " inconclusive=" << totals.inconclusive
            << "\nResults: " << (value.output / "results.csv")
            << '\n';

        if (!value.keep_files)
        {
            std::error_code error;
            (void)std::filesystem::remove_all(
                cases_directory,
                error
            );
        }

        if (totals.failed != 0)
            throw std::runtime_error(
                "stress suite reported failed cases"
            );
    }
}

int main(int argc, char** argv)
{
    try
    {
        const options value = parse_options(argc, argv);

        std::filesystem::create_directories(value.root);
        std::filesystem::create_directories(value.output);

        std::cout
            << "Windows edit stress profile=" << value.profile
            << " large_size_bytes=" << value.large_size
            << " short_count=" << value.short_count
            << " long_count=" << value.long_count
            << " seed=" << value.seed
            << "\nRoot: " << value.root
            << "\nOutput: " << value.output
            << "\n\n";

        result_logger logger(value.output);
        run_suite(value, logger);
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "windows_edit_stress failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
