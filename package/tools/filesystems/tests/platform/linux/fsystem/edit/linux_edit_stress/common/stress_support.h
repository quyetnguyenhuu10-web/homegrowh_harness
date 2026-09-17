#pragma once

#ifndef __linux__
#error "Linux edit stress support is Linux-only"
#endif

#include <fsystem>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace linux_edit_stress
{
    using json = nlohmann::json;
    using clock_type = std::chrono::steady_clock;

    inline constexpr int result_schema_version = 1;
    inline constexpr std::uint64_t kibibyte = 1024;
    inline constexpr std::uint64_t mebibyte = 1024 * kibibyte;
    inline constexpr std::uint64_t gibibyte = 1024 * mebibyte;
    inline constexpr std::size_t io_buffer_capacity = 1 * 1024 * 1024;
    inline constexpr std::uint64_t lock_hold_milliseconds = 5000;
    inline constexpr std::uint64_t fnv_offset_basis =
        14695981039346656037ull;
    inline constexpr std::uint64_t fnv_prime = 1099511628211ull;

    struct options
    {
        std::string profile = "smoke";
        std::filesystem::path root;
        std::filesystem::path output;
        std::uint64_t seed = 0;
        std::uint32_t interference_wait_ms = 120000;
        bool keep_files = false;
        std::uint64_t repeat = 1;
    };

    struct profile_settings
    {
        std::vector<std::uint64_t> file_sizes;
        std::uint64_t reference_file_size = 16 * mebibyte;
        std::uint64_t interference_file_size = 32 * mebibyte;
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

    enum class interference_kind
    {
        none,
        modify,
        delete_file,
        rename_file,
        lock,
    };

    struct result_record
    {
        std::string category_id;
        std::string category_title;
        std::string profile;
        std::string case_id;
        std::string scenario;
        std::string expected;
        std::string actual_note;
        std::string outcome;
        std::string detail;
        std::string pattern_position;
        std::string replacement_bucket;
        std::string interference_action;
        std::string interference_phase;
        std::uint64_t file_size_bytes = 0;
        std::uint64_t pattern_offset_bytes = 0;
        std::uint64_t old_size_bytes = 0;
        std::uint64_t new_size_bytes = 0;
        std::uint64_t duration_ms = 0;
        std::uint64_t working_set_bytes = 0;
        std::uint64_t peak_working_set_bytes = 0;
        std::uint64_t ram_harness_bytes = 0;
        std::uint64_t ram_edit_bytes = 0;
        std::uint64_t disk_free_before_bytes = 0;
        std::uint64_t disk_free_after_bytes = 0;
        std::int64_t disk_free_delta_bytes = 0;
        std::uint64_t disk_consumed_bytes = 0;
        std::uint64_t interference_attempts = 0;
        std::uint32_t interference_first_error = 0;
        std::uint32_t error_code = 0;
        bool interference_succeeded = false;
        bool interference_in_window = false;
        bool lock_at_replace = false;
        bool replace_attempted = false;
        bool content_ok = false;
    };

    struct run_totals
    {
        std::size_t total = 0;
        std::size_t passed = 0;
        std::size_t failed = 0;
        std::size_t inconclusive = 0;
    };

    struct edit_case_spec
    {
        std::string id;
        std::string scenario;
        std::string expected;
        std::string pattern_position;
        std::string replacement_bucket;
        std::filesystem::path path;
        std::uint64_t file_size = 0;
        std::uint64_t marker_offset = 0;
        std::string old_data;
        std::string new_data;
        bool marker_present = true;
        bool exact_input = false;
        std::string input_content;
        std::string expected_content;
    };

    struct interference_case_spec
    {
        edit_case_spec edit;
        interference_kind action = interference_kind::none;
        std::uint64_t interference_offset = 0;
        bool lock_before_edit = false;
    };

    struct interference_observation
    {
        std::uint64_t attempts = 0;
        std::uint32_t first_error = 0;
        bool succeeded = false;
        clock_type::time_point success_time{};
        std::filesystem::path renamed_path;
    };

    class unique_fd
    {
    public:
        unique_fd() noexcept = default;

        explicit unique_fd(int fd) noexcept
            : fd_(fd)
        {
        }

        ~unique_fd()
        {
            reset();
        }

        unique_fd(const unique_fd&) = delete;
        unique_fd& operator=(const unique_fd&) = delete;

        unique_fd(unique_fd&& other) noexcept
            : fd_(other.release())
        {
        }

        unique_fd& operator=(unique_fd&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                fd_ = other.release();
            }

            return *this;
        }

        int get() const noexcept
        {
            return fd_;
        }

        explicit operator bool() const noexcept
        {
            return fd_ >= 0;
        }

        int release() noexcept
        {
            const int result = fd_;
            fd_ = -1;
            return result;
        }

        void reset(int fd = -1) noexcept
        {
            if (fd_ >= 0)
                (void)::close(fd_);

            fd_ = fd;
        }

    private:
        int fd_ = -1;
    };

    inline std::uint32_t current_errno() noexcept
    {
        return static_cast<std::uint32_t>(errno);
    }

    inline std::uint64_t parse_uint64(
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

    inline void print_usage(std::string_view executable)
    {
        std::cout
            << executable << " options:\n"
            << "  --profile smoke|standard|extreme\n"
            << "  --root <directory>\n"
            << "  --output <directory>\n"
            << "  --seed <integer>\n"
            << "  --interference-wait-ms <integer>\n"
            << "  --repeat <integer>\n"
            << "  --keep-files\n";
    }

    inline options parse_options(int argc, char** argv)
    {
        options result;

        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument(argv[index]);

            if (argument == "--help" || argument == "-h")
            {
                print_usage(argv[0]);
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
            else if (argument == "--seed")
            {
                result.seed = parse_uint64(next_value(), argument);
            }
            else if (argument == "--repeat")
            {
                result.repeat = parse_uint64(next_value(), argument);
            }
            else if (argument == "--interference-wait-ms")
            {
                const std::uint64_t wait = parse_uint64(
                    next_value(),
                    argument
                );

                if (wait > std::numeric_limits<std::uint32_t>::max())
                    throw std::runtime_error(
                        "--interference-wait-ms is too large"
                    );

                result.interference_wait_ms =
                    static_cast<std::uint32_t>(wait);
            }
            else if (argument == "--keep-files")
            {
                result.keep_files = true;
            }
            else if (argument == "--help" || argument == "-h")
            {
                print_usage(argv[0]);
                std::exit(0);
            }
            else
            {
                throw std::runtime_error(
                    "unknown option: " + std::string(argument)
                );
            }
        }

        if (result.profile != "smoke" &&
            result.profile != "standard" &&
            result.profile != "extreme")
        {
            throw std::runtime_error(
                "profile must be smoke, standard, or extreme"
            );
        }

        if (result.repeat < 1)
        {
            throw std::runtime_error(
                "--repeat must be at least 1"
            );
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
                ("fsystem-linux-edit-stress-" +
                 std::to_string(timestamp));
        }

        if (result.output.empty())
            result.output = result.root / "results";

        return result;
    }

    inline profile_settings settings_for(std::string_view profile)
    {
        if (profile == "smoke")
        {
            return profile_settings{
                {1 * mebibyte, 4 * mebibyte, 16 * mebibyte},
                16 * mebibyte,
                32 * mebibyte,
            };
        }

        if (profile == "standard")
        {
            return profile_settings{
                {1 * mebibyte, 8 * mebibyte, 64 * mebibyte, 512 * mebibyte},
                64 * mebibyte,
                128 * mebibyte,
            };
        }

        return profile_settings{
            {4 * mebibyte, 32 * mebibyte, 256 * mebibyte,
             1 * gibibyte, 4 * gibibyte},
            256 * mebibyte,
            512 * mebibyte,
        };
    }

    inline std::string position_name(
        std::uint64_t offset,
        std::uint64_t file_size,
        std::size_t marker_size
    )
    {
        if (offset == 0)
            return "beginning";

        if (file_size >= marker_size &&
            offset + marker_size >= file_size)
        {
            return "end";
        }

        return "middle";
    }

    inline std::string make_marker(
        std::string_view prefix,
        std::size_t index,
        std::size_t extra_length = 16
    )
    {
        std::string result;
        result.reserve(prefix.size() + 32 + extra_length);
        result.append(prefix);
        result.push_back('_');
        result.append(std::to_string(index));
        result.append("_MARKER_");

        for (std::size_t count = 0; count < extra_length; ++count)
            result.push_back(
                static_cast<char>('A' + ((count + index) % 26))
            );

        return result;
    }

    inline std::string make_payload(
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

    inline memory_snapshot current_memory() noexcept
    {
        memory_snapshot result;
        std::ifstream statm("/proc/self/statm");
        std::uint64_t total_pages = 0;
        std::uint64_t resident_pages = 0;
        const long page_size = ::sysconf(_SC_PAGESIZE);

        if (
            statm &&
            page_size > 0 &&
            (statm >> total_pages >> resident_pages)
        )
        {
            result.working_set_bytes = resident_pages *
                static_cast<std::uint64_t>(page_size);
        }

        struct rusage usage{};

        if (::getrusage(RUSAGE_SELF, &usage) == 0 && usage.ru_maxrss > 0)
        {
            result.peak_working_set_bytes = static_cast<std::uint64_t>(
                usage.ru_maxrss
            ) * kibibyte;
        }

        return result;
    }

    inline std::uint64_t disk_free_bytes(
        const std::filesystem::path& path
    ) noexcept
    {
        struct statvfs status{};

        if (::statvfs(path.c_str(), &status) != 0)
            return 0;

        return static_cast<std::uint64_t>(status.f_bavail) *
            static_cast<std::uint64_t>(status.f_frsize);
    }

    inline bool write_repeated(
        std::ofstream& output,
        char value,
        std::uint64_t count
    )
    {
        const std::vector<char> buffer(io_buffer_capacity, value);

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

    inline bool write_exact(
        const std::filesystem::path& path,
        std::string_view content
    )
    {
        std::ofstream output(
            path,
            std::ios::binary | std::ios::trunc
        );

        if (!output)
            return false;

        output.write(
            content.data(),
            static_cast<std::streamsize>(content.size())
        );
        output.flush();
        return output.good();
    }

    inline bool write_pattern_file(
        const std::filesystem::path& path,
        std::uint64_t file_size,
        std::uint64_t marker_offset,
        std::string_view marker
    )
    {
        if (marker_offset > file_size ||
            marker.size() > file_size - marker_offset)
        {
            return false;
        }

        std::ofstream output(
            path,
            std::ios::binary | std::ios::trunc
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

    inline void hash_bytes(
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

    inline void hash_repeated(
        std::uint64_t& hash,
        char value,
        std::uint64_t count
    )
    {
        const std::vector<char> buffer(io_buffer_capacity, value);

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

    inline fingerprint fingerprint_file(
        const std::filesystem::path& path
    ) noexcept
    {
        fingerprint result;
        std::ifstream input(path, std::ios::binary);

        if (!input)
        {
            result.error = current_errno();
            if (result.error == 0)
                result.error = ENOENT;
            return result;
        }

        std::vector<char> buffer(io_buffer_capacity);
        std::uint64_t hash = fnv_offset_basis;

        while (true)
        {
            input.read(
                buffer.data(),
                static_cast<std::streamsize>(buffer.size())
            );

            const std::streamsize bytes_read = input.gcount();

            if (bytes_read > 0)
            {
                hash_bytes(
                    hash,
                    buffer.data(),
                    static_cast<std::size_t>(bytes_read)
                );
                result.size += static_cast<std::uint64_t>(bytes_read);
            }

            if (input.bad())
            {
                result.error = EIO;
                return result;
            }

            if (input.eof())
                break;

            if (bytes_read == 0)
            {
                result.error = EIO;
                return result;
            }
        }

        result.ok = true;
        result.hash = hash;
        return result;
    }

    inline std::uint64_t expected_hash(
        std::uint64_t file_size,
        std::uint64_t marker_offset,
        std::string_view replacement
    )
    {
        if (marker_offset > file_size ||
            replacement.size() > file_size - marker_offset)
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

    inline bool matches_pattern(
        const std::filesystem::path& path,
        std::uint64_t file_size,
        std::uint64_t marker_offset,
        std::string_view replacement,
        std::uint32_t& error
    ) noexcept
    {
        if (marker_offset > file_size ||
            replacement.size() > file_size - marker_offset)
        {
            error = EINVAL;
            return false;
        }

        const fingerprint actual = fingerprint_file(path);

        if (!actual.ok)
        {
            error = actual.error;
            return false;
        }

        const bool matches =
            actual.size == file_size &&
            actual.hash == expected_hash(
                file_size,
                marker_offset,
                replacement
            );

        if (!matches)
            error = EINVAL;

        return matches;
    }

    inline bool read_exact(
        const std::filesystem::path& path,
        std::string& content,
        std::uint32_t& error
    ) noexcept
    {
        std::ifstream input(path, std::ios::binary);

        if (!input)
        {
            error = current_errno();
            if (error == 0)
                error = ENOENT;
            return false;
        }

        content.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );

        if (input.bad())
        {
            error = EIO;
            return false;
        }

        return true;
    }

    inline bool write_case_input(
        const edit_case_spec& spec,
        std::uint32_t& error
    ) noexcept
    {
        std::error_code directory_error;
        std::filesystem::create_directories(
            spec.path.parent_path(),
            directory_error
        );

        if (directory_error)
        {
            error = static_cast<std::uint32_t>(
                directory_error.value()
            );
            return false;
        }

        const bool written = spec.exact_input
            ? write_exact(spec.path, spec.input_content)
            : write_pattern_file(
                spec.path,
                spec.file_size,
                spec.marker_present ? spec.marker_offset : 0,
                spec.marker_present
                    ? std::string_view(spec.old_data)
                    : std::string_view()
            );

        if (!written)
        {
            error = EIO;
            return false;
        }

        return true;
    }

    inline bool verify_case_output(
        const edit_case_spec& spec,
        bool replacement_expected,
        std::uint32_t& error
    ) noexcept
    {
        if (spec.exact_input)
        {
            std::string actual;

            if (!read_exact(spec.path, actual, error))
                return false;

            if (actual != spec.expected_content)
            {
                error = EINVAL;
                return false;
            }

            return true;
        }

        if (spec.old_data.size() > spec.file_size)
        {
            error = EINVAL;
            return false;
        }

        const std::uint64_t expected_size = replacement_expected
            ? spec.file_size - spec.old_data.size() +
                spec.new_data.size()
            : spec.file_size;
        const std::uint64_t expected_offset = replacement_expected
            ? spec.marker_offset
            : (spec.marker_present ? spec.marker_offset : 0);
        const std::string_view expected_data = replacement_expected
            ? std::string_view(spec.new_data)
            : (spec.marker_present
                ? std::string_view(spec.old_data)
                : std::string_view());

        return matches_pattern(
            spec.path,
            expected_size,
            expected_offset,
            expected_data,
            error
        );
    }

    inline fsystem::EditNote expected_note_for(
        std::string_view expected
    ) noexcept
    {
        if (expected == "success")
            return fsystem::EditNote::none;

        if (expected == "not_found")
            return fsystem::EditNote::old_data_not_found;

        if (expected == "overlap")
            return fsystem::EditNote::old_data_occurrences_overlap;

        return fsystem::EditNote::old_data_appears_more_than_once;
    }

    inline std::string note_name(fsystem::EditNote note)
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
        }

        return "unknown";
    }

    inline std::string action_name(interference_kind action)
    {
        switch (action)
        {
        case interference_kind::none:
            return "none";
        case interference_kind::modify:
            return "modify";
        case interference_kind::delete_file:
            return "delete";
        case interference_kind::rename_file:
            return "rename";
        case interference_kind::lock:
            return "lock";
        }

        return "unknown";
    }

    inline result_record base_result(
        const options& value,
        std::string_view category_id,
        std::string_view category_title,
        const edit_case_spec& spec
    )
    {
        result_record result;
        result.category_id = category_id;
        result.category_title = category_title;
        result.profile = value.profile;
        result.case_id = spec.id;
        result.scenario = spec.scenario;
        result.expected = spec.expected;
        result.pattern_position = spec.pattern_position;
        result.replacement_bucket = spec.replacement_bucket;
        result.file_size_bytes = spec.file_size;
        result.pattern_offset_bytes = spec.marker_offset;
        result.old_size_bytes = spec.old_data.size();
        result.new_size_bytes = spec.new_data.size();
        result.interference_action = "none";
        result.interference_phase = "none";
        return result;
    }

    inline std::filesystem::path case_path(
        const options& value,
        std::string_view category_id,
        std::string_view case_id
    )
    {
        return value.root /
            std::filesystem::path(category_id) /
            (std::string(case_id) + ".bin");
    }

    inline void finish_metrics(
        result_record& result,
        const options& value,
        const memory_snapshot& memory_before,
        const memory_snapshot& memory_pre_edit,
        clock_type::time_point started
    )
    {
        result.disk_free_after_bytes = disk_free_bytes(value.root);
        result.disk_free_delta_bytes =
            static_cast<std::int64_t>(result.disk_free_after_bytes) -
            static_cast<std::int64_t>(result.disk_free_before_bytes);

        if (result.disk_free_before_bytes > result.disk_free_after_bytes)
        {
            result.disk_consumed_bytes =
                result.disk_free_before_bytes -
                result.disk_free_after_bytes;
        }

        const memory_snapshot memory_after = current_memory();
        result.working_set_bytes = memory_after.working_set_bytes;
        result.peak_working_set_bytes = std::max(
            memory_before.peak_working_set_bytes,
            memory_after.peak_working_set_bytes
        );
        result.ram_harness_bytes = memory_pre_edit.working_set_bytes;
        // New high-water pushed while this case ran. Zero when the
        // case reused already-faulted pages: its real marginal cost.
        // Never carry the old peak forward.
        result.ram_edit_bytes =
            memory_after.peak_working_set_bytes >
                memory_before.peak_working_set_bytes
            ? memory_after.peak_working_set_bytes -
                memory_before.peak_working_set_bytes
            : 0;
        result.duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                clock_type::now() - started
            ).count()
        );
    }

    inline void remove_case_files(
        const edit_case_spec& spec,
        const interference_observation& observation,
        bool keep_files
    )
    {
        if (keep_files)
            return;

        std::error_code error;
        (void)std::filesystem::remove(spec.path, error);

        if (!observation.renamed_path.empty())
            (void)std::filesystem::remove(
                observation.renamed_path,
                error
            );
    }

    inline result_record run_edit_case(
        const options& value,
        std::string_view category_id,
        std::string_view category_title,
        const edit_case_spec& spec
    )
    {
        result_record result = base_result(
            value,
            category_id,
            category_title,
            spec
        );
        const auto started = clock_type::now();
        const memory_snapshot memory_before = current_memory();
        memory_snapshot memory_pre_edit = memory_before;
        result.disk_free_before_bytes = disk_free_bytes(value.root);
        interference_observation no_interference;

        try
        {
            std::uint32_t input_error = 0;

            if (!write_case_input(spec, input_error))
            {
                result.outcome = "fail";
                result.error_code = input_error;
                result.detail = "unable to create input file";
            }
            else
            {
                memory_pre_edit = current_memory();

                const fsystem::EditResult edit_result = fsystem::edit(
                    spec.path,
                    spec.old_data,
                    spec.new_data
                );

                result.error_code = edit_result.error;
                result.actual_note = note_name(edit_result.note);
                result.replace_attempted = edit_result.replace_attempted;

                std::uint32_t verification_error = 0;
                result.content_ok = verify_case_output(
                    spec,
                    spec.expected == "success",
                    verification_error
                );

                const bool edit_ok =
                    edit_result.error == 0 &&
                    edit_result.note == expected_note_for(spec.expected);

                result.outcome = edit_ok && result.content_ok
                    ? "pass"
                    : "fail";

                if (!result.content_ok)
                {
                    result.detail =
                        "verification_error=" +
                        std::to_string(verification_error);
                }
            }
        }
        catch (const std::exception& exception)
        {
            result.outcome = "fail";
            result.detail =
                std::string("exception=") + exception.what();
        }

        finish_metrics(
            result,
            value,
            memory_before,
            memory_pre_edit,
            started
        );
        remove_case_files(spec, no_interference, value.keep_files);
        return result;
    }

    inline bool try_modify(
        const interference_case_spec& spec,
        std::uint32_t& error
    ) noexcept
    {
        const int raw_fd = ::open(
            spec.edit.path.c_str(),
            O_WRONLY | O_CLOEXEC
        );

        if (raw_fd < 0)
        {
            error = current_errno();
            return false;
        }

        unique_fd file(raw_fd);
        constexpr std::string_view marker = "INTERFERENCE";
        std::size_t position = 0;

        while (position < marker.size())
        {
            const ssize_t bytes_written = ::pwrite(
                file.get(),
                marker.data() + position,
                marker.size() - position,
                static_cast<off_t>(spec.interference_offset + position)
            );

            if (bytes_written < 0)
            {
                if (errno == EINTR)
                    continue;

                error = current_errno();
                return false;
            }

            if (bytes_written == 0)
            {
                error = EIO;
                return false;
            }

            position += static_cast<std::size_t>(bytes_written);
        }

        if (::fsync(file.get()) < 0)
        {
            error = current_errno();
            return false;
        }

        return true;
    }

    inline bool try_delete(
        const interference_case_spec& spec,
        std::uint32_t& error
    ) noexcept
    {
        if (::unlink(spec.edit.path.c_str()) == 0)
            return true;

        error = current_errno();
        return false;
    }

    inline bool try_lock(
        const interference_case_spec& spec,
        unique_fd& handle,
        std::uint32_t& error
    ) noexcept
    {
        const int raw_fd = ::open(
            spec.edit.path.c_str(),
            O_RDWR | O_CLOEXEC
        );

        if (raw_fd < 0)
        {
            error = current_errno();
            return false;
        }

        unique_fd candidate(raw_fd);

        if (::flock(candidate.get(), LOCK_EX | LOCK_NB) != 0)
        {
            error = current_errno();
            return false;
        }

        handle = std::move(candidate);
        return true;
    }

    inline void run_interferer(
        const interference_case_spec& spec,
        const options& value,
        std::atomic_bool& stop_requested,
        interference_observation& observation
    ) noexcept
    {
        // Fire just after the edit starts, then keep acting until the
        // edit finishes, so the interference is guaranteed to land
        // inside the edit window instead of racing it from 25 ms away.
        std::this_thread::sleep_for(
            std::chrono::milliseconds(1)
        );

        const auto deadline =
            clock_type::now() +
            std::chrono::milliseconds(value.interference_wait_ms);

        if (stop_requested.load(std::memory_order_acquire))
            return;

        if (spec.action == interference_kind::lock)
        {
            unique_fd lock_handle;

            while (
                !stop_requested.load(std::memory_order_acquire) &&
                clock_type::now() < deadline
            )
            {
                ++observation.attempts;
                std::uint32_t error = 0;

                if (try_lock(spec, lock_handle, error))
                {
                    observation.succeeded = true;
                    observation.success_time = clock_type::now();

                    const auto lock_deadline = clock_type::now() +
                        std::chrono::milliseconds(
                            lock_hold_milliseconds
                        );

                    while (
                        !stop_requested.load(
                            std::memory_order_acquire
                        ) &&
                        clock_type::now() < lock_deadline
                    )
                    {
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(10)
                        );
                    }

                    return;
                }

                if (observation.first_error == 0)
                    observation.first_error = error;

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1)
                );
            }

            return;
        }

        std::filesystem::path renamed_path = spec.edit.path;
        renamed_path += ".interference-renamed";
        bool renamed_side = false;

        while (
            !stop_requested.load(std::memory_order_acquire) &&
            clock_type::now() < deadline
        )
        {
            ++observation.attempts;

            std::uint32_t error = 0;
            bool success = false;

            switch (spec.action)
            {
            case interference_kind::modify:
                success = try_modify(spec, error);
                break;
            case interference_kind::delete_file:
                success = try_delete(spec, error);
                break;
            case interference_kind::rename_file:
            {
                const std::filesystem::path& from =
                    renamed_side ? renamed_path : spec.edit.path;
                const std::filesystem::path& to =
                    renamed_side ? spec.edit.path : renamed_path;

                if (::rename(
                        from.c_str(),
                        to.c_str()
                    ) == 0)
                {
                    observation.renamed_path = renamed_path;
                    renamed_side = !renamed_side;
                    success = true;
                }
                else
                {
                    error = current_errno();
                }
                break;
            }
            case interference_kind::lock:
            case interference_kind::none:
                return;
            }

            if (success)
            {
                if (!observation.succeeded)
                {
                    observation.succeeded = true;
                    observation.success_time = clock_type::now();
                    observation.first_error = 0;
                }

                if (spec.action == interference_kind::delete_file)
                    return;
            }
            else if (observation.first_error == 0)
            {
                observation.first_error = error;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(1)
            );
        }
    }

    inline void set_interference_fields(
        result_record& result,
        interference_kind action,
        bool lock_before_edit,
        const interference_observation& observation,
        clock_type::time_point edit_finished_at
    )
    {
        result.interference_action = action_name(action);
        result.interference_phase = lock_before_edit
            ? "before_edit"
            : "during_edit";
        result.interference_attempts = observation.attempts;
        result.interference_first_error = observation.first_error;
        result.interference_succeeded = observation.succeeded;
        result.interference_in_window =
            observation.succeeded &&
            observation.success_time <= edit_finished_at;
    }

    inline result_record run_interference_case(
        const options& value,
        std::string_view category_id,
        std::string_view category_title,
        const interference_case_spec& spec
    )
    {
        result_record result = base_result(
            value,
            category_id,
            category_title,
            spec.edit
        );
        const auto started = clock_type::now();
        const memory_snapshot memory_before = current_memory();
        memory_snapshot memory_pre_edit = memory_before;
        result.disk_free_before_bytes = disk_free_bytes(value.root);
        interference_observation observation;
        std::atomic_bool stop_interference{false};
        std::thread interference_thread;
        unique_fd pre_edit_lock;
        bool edit_completed = false;
        clock_type::time_point edit_finished_at = started;

        try
        {
            std::uint32_t input_error = 0;

            if (!write_case_input(spec.edit, input_error))
            {
                result.outcome = "fail";
                result.error_code = input_error;
                result.detail = "unable to create input file";
            }
            else
            {
                if (spec.lock_before_edit)
                {
                    ++observation.attempts;
                    std::uint32_t lock_error = 0;

                    if (!try_lock(spec, pre_edit_lock, lock_error))
                        observation.first_error = lock_error;
                    else
                    {
                        observation.succeeded = true;
                        observation.success_time = clock_type::now();
                    }
                }
                else
                {
                    interference_thread = std::thread(
                        [&]()
                        {
                            run_interferer(
                                spec,
                                value,
                                stop_interference,
                                observation
                            );
                        }
                    );
                }

                memory_pre_edit = current_memory();

                const fsystem::EditResult edit_result = fsystem::edit(
                    spec.edit.path,
                    spec.edit.old_data,
                    spec.edit.new_data
                );

                edit_completed = true;
                edit_finished_at = clock_type::now();
                pre_edit_lock.reset();
                stop_interference.store(
                    true,
                    std::memory_order_release
                );

                if (interference_thread.joinable())
                    interference_thread.join();

                result.error_code = edit_result.error;
                result.actual_note = note_name(edit_result.note);
                result.replace_attempted = edit_result.replace_attempted;
                set_interference_fields(
                    result,
                    spec.action,
                    spec.lock_before_edit,
                    observation,
                    edit_finished_at
                );

                std::uint32_t verification_error = 0;

                if (spec.lock_before_edit)
                {
                    const bool replacement_expected =
                        edit_result.error == 0 &&
                        edit_result.note == fsystem::EditNote::none;
                    result.content_ok = verify_case_output(
                        spec.edit,
                        replacement_expected,
                        verification_error
                    );
                    result.outcome =
                        observation.succeeded && result.content_ok
                            ? "pass"
                            : "fail";
                    result.detail = result.outcome == "pass"
                        ? "advisory exclusive lock held before edit; "
                          "Linux rename does not enforce advisory locks"
                        : "exclusive advisory lock was not acquired or "
                          "content verification failed";
                }
                else if (!observation.succeeded)
                {
                    result.outcome = "inconclusive";
                    result.detail = "interference action did not acquire target";
                }
                else if (!result.interference_in_window)
                {
                    result.outcome = "inconclusive";
                    result.detail =
                        "interference was acquired after edit completed";
                }
                else if (spec.action == interference_kind::lock)
                {
                    result.content_ok = edit_result.error == 0
                        ? verify_case_output(
                            spec.edit,
                            true,
                            verification_error
                        )
                        : verify_case_output(
                            spec.edit,
                            false,
                            verification_error
                        );
                    result.outcome = result.content_ok
                        ? "pass"
                        : "fail";
                    result.detail = result.outcome == "pass"
                        ? "advisory exclusive lock overlapped edit; "
                          "Linux rename is not blocked by flock"
                        : "content verification failed while advisory "
                          "lock overlapped edit";
                }
                else
                {
                    const bool edit_observed_interference =
                        edit_result.error != 0 ||
                        edit_result.note == fsystem::EditNote::file_changed ||
                        edit_result.note ==
                            fsystem::EditNote::old_data_not_found;

                    result.outcome = edit_observed_interference
                        ? "pass"
                        : "fail";
                    result.detail = edit_observed_interference
                        ? "edit reported or observed the concurrent "
                          "interference"
                        : "edit completed as if no interference occurred";
                }

                if (!result.content_ok && verification_error != 0)
                {
                    result.detail +=
                        "; verification_error=" +
                        std::to_string(verification_error);
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

        pre_edit_lock.reset();
        result.interference_attempts = observation.attempts;
        result.interference_first_error = observation.first_error;
        result.interference_succeeded = observation.succeeded;
        result.disk_free_after_bytes = disk_free_bytes(value.root);
        result.disk_free_delta_bytes =
            static_cast<std::int64_t>(result.disk_free_after_bytes) -
            static_cast<std::int64_t>(result.disk_free_before_bytes);

        if (result.disk_free_before_bytes > result.disk_free_after_bytes)
        {
            result.disk_consumed_bytes =
                result.disk_free_before_bytes -
                result.disk_free_after_bytes;
        }

        const memory_snapshot memory_after = current_memory();
        result.working_set_bytes = memory_after.working_set_bytes;
        result.peak_working_set_bytes = std::max(
            memory_before.peak_working_set_bytes,
            memory_after.peak_working_set_bytes
        );
        result.ram_harness_bytes = memory_pre_edit.working_set_bytes;
        // New high-water pushed while this case ran. Zero when the
        // case reused already-faulted pages: its real marginal cost.
        // Never carry the old peak forward.
        result.ram_edit_bytes =
            memory_after.peak_working_set_bytes >
                memory_before.peak_working_set_bytes
            ? memory_after.peak_working_set_bytes -
                memory_before.peak_working_set_bytes
            : 0;
        result.duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                clock_type::now() - started
            ).count()
        );

        remove_case_files(
            spec.edit,
            observation,
            value.keep_files
        );
        return result;
    }

    inline json to_json(const result_record& result)
    {
        return json{
            {"schema_version", result_schema_version},
            {"category", {
                {"id", result.category_id},
                {"title", result.category_title},
            }},
            {"profile", result.profile},
            {"case", {
                {"id", result.case_id},
                {"scenario", result.scenario},
                {"expected", result.expected},
            }},
            {"dimensions", {
                {"file_size_bytes", result.file_size_bytes},
                {"pattern_offset_bytes", result.pattern_offset_bytes},
                {"pattern_position", result.pattern_position},
                {"old_size_bytes", result.old_size_bytes},
                {"new_size_bytes", result.new_size_bytes},
                {"replacement_bucket", result.replacement_bucket},
            }},
            {"metrics", {
                {"duration_ms", result.duration_ms},
                {"working_set_bytes", result.working_set_bytes},
                {"peak_working_set_bytes", result.peak_working_set_bytes},
                {"ram_harness_bytes", result.ram_harness_bytes},
                {"ram_edit_bytes", result.ram_edit_bytes},
                {"disk_free_before_bytes", result.disk_free_before_bytes},
                {"disk_free_after_bytes", result.disk_free_after_bytes},
                {"disk_free_delta_bytes", result.disk_free_delta_bytes},
                {"disk_consumed_bytes", result.disk_consumed_bytes},
            }},
            {"result", {
                {"outcome", result.outcome},
                {"actual_note", result.actual_note},
                {"error_code", result.error_code},
                {"replace_attempted", result.replace_attempted},
                {"content_ok", result.content_ok},
            }},
            {"interference", {
                {"action", result.interference_action},
                {"phase", result.interference_phase},
                {"attempts", result.interference_attempts},
                {"first_error", result.interference_first_error},
                {"succeeded", result.interference_succeeded},
                {"in_edit_window", result.interference_in_window},
                {"lock_at_replace", result.lock_at_replace},
            }},
            {"detail", result.detail},
        };
    }

    class result_writer
    {
    public:
        result_writer(
            const std::filesystem::path& output,
            std::string_view category_id,
            std::string_view category_title,
            std::string_view profile
        )
            : output_(output),
              category_id_(category_id),
              category_title_(category_title),
              profile_(profile)
        {
            std::error_code error;
            std::filesystem::create_directories(output_, error);
            jsonl_.open(output_ / "results.jsonl", std::ios::trunc);

            if (error || !jsonl_)
                throw std::runtime_error(
                    "unable to create result output directory"
                );

            write_schema();
        }

        void write(const result_record& result)
        {
            jsonl_ << to_json(result).dump() << '\n';
            jsonl_.flush();

            ++totals_.total;

            if (result.outcome == "pass")
                ++totals_.passed;
            else if (result.outcome == "fail")
                ++totals_.failed;
            else
                ++totals_.inconclusive;

            std::cout
                << result.case_id
                << " outcome=" << result.outcome
                << " duration_ms=" << result.duration_ms
                << " error=" << result.error_code
                << " note=" << result.actual_note
                << '\n';
        }

        bool finish()
        {
            const json summary{
                {"schema_version", result_schema_version},
                {"category_id", category_id_},
                {"category_title", category_title_},
                {"profile", profile_},
                {"total", totals_.total},
                {"passed", totals_.passed},
                {"failed", totals_.failed},
                {"inconclusive", totals_.inconclusive},
            };

            std::ofstream summary_file(output_ / "summary.json");
            summary_file << summary.dump(2) << '\n';

            std::ofstream text_summary(output_ / "summary.txt");
            text_summary
                << "schema_version=" << result_schema_version << '\n'
                << "category_id=" << category_id_ << '\n'
                << "profile=" << profile_ << '\n'
                << "total=" << totals_.total << '\n'
                << "passed=" << totals_.passed << '\n'
                << "failed=" << totals_.failed << '\n'
                << "inconclusive=" << totals_.inconclusive << '\n';

            return totals_.failed == 0;
        }

    private:
        void write_schema()
        {
            const json schema{
                {"schema_version", result_schema_version},
                {"format", "jsonl"},
                {"record", {
                    {"category", "id and display title"},
                    {"profile", "smoke, standard, or extreme"},
                    {"case", "id, scenario, and expected behavior"},
                    {"dimensions", "file/pattern/replacement dimensions"},
                    {"metrics", "time, memory, and disk measurements"},
                    {"result", "outcome, error, note, and verification"},
                    {"interference", "interference observations"},
                    {"detail", "human-readable diagnostic detail"},
                }},
                {"metric_units", {
                    {"duration_ms", "milliseconds"},
                    {"*_bytes", "bytes"},
                }},
            };

            std::ofstream schema_file(output_ / "schema.json");
            schema_file << schema.dump(2) << '\n';
        }

        std::filesystem::path output_;
        std::string category_id_;
        std::string category_title_;
        std::string profile_;
        std::ofstream jsonl_;
        run_totals totals_;
    };

    template<typename callback_type>
    inline int run_category(
        int argc,
        char** argv,
        std::string_view category_id,
        std::string_view category_title,
        callback_type&& callback
    )
    {
        try
        {
            const options value = parse_options(argc, argv);
            std::filesystem::create_directories(value.root);

            result_writer writer(
                value.output,
                category_id,
                category_title,
                value.profile
            );

            callback(value, writer);
            return writer.finish() ? 0 : 1;
        }
        catch (const std::exception& exception)
        {
            std::cerr
                << "linux edit stress failed: "
                << exception.what()
                << '\n';
            return 1;
        }
    }
}
