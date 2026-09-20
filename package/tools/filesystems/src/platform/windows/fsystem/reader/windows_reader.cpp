#include "windows_reader.h"

#include "../../../../config/reader_config.h"
#include "fsystem/read/read_detail.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace fsystem::windows
{
    namespace
    {
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

        ReadResult read_one(const ReadRequest& request)
        {
            ReadResult result{};
            result.path = request.path;
            result.start_line = request.start_line;
            result.end_line = request.end_line;

            if (
                request.start_line == 0 ||
                request.end_line < request.start_line
            )
            {
                result.error = ERROR_INVALID_PARAMETER;
                return result;
            }

            constexpr std::uint32_t block_size =
                fsystem::config::kReadBlockSize;

            HANDLE raw_handle = CreateFileW(
                request.path.c_str(),
                GENERIC_READ,
                FILE_SHARE_READ,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
                nullptr
            );

            if (raw_handle == INVALID_HANDLE_VALUE)
            {
                result.error = GetLastError();
                return result;
            }

            unique_handle handle(raw_handle);
            HANDLE hFile = handle.get();

            LARGE_INTEGER file_size_info{};

            if (!GetFileSizeEx(hFile, &file_size_info))
            {
                result.error = GetLastError();
                return result;
            }

            if (file_size_info.QuadPart < 0)
            {
                result.error = ERROR_INVALID_DATA;
                return result;
            }

            const std::uint64_t file_size =
                static_cast<std::uint64_t>(file_size_info.QuadPart);

            if (file_size == 0)
                return result;

            std::vector<char> buffer(block_size);
            detail::line_range_collector collector(
                request.start_line,
                request.end_line,
                result.content
            );

            std::uint64_t position = 0;

            while (position < file_size)
            {
                const std::uint64_t remaining = file_size - position;
                const DWORD bytes_to_read = static_cast<DWORD>(
                    remaining < block_size ? remaining : block_size
                );

                OVERLAPPED overlapped{};
                overlapped.Offset = static_cast<DWORD>(
                    position % (1ULL << 32)
                );
                overlapped.OffsetHigh = static_cast<DWORD>(
                    position / (1ULL << 32)
                );

                BOOL success = ReadFile(
                    hFile,
                    buffer.data(),
                    bytes_to_read,
                    nullptr,
                    &overlapped
                );

                if (!success)
                {
                    const DWORD error = GetLastError();

                    if (error != ERROR_IO_PENDING)
                    {
                        result.error = error;
                        result.content.clear();
                        return result;
                    }
                }

                DWORD bytes_read = 0;
                const BOOL completed = GetOverlappedResult(
                    hFile,
                    &overlapped,
                    &bytes_read,
                    TRUE
                );

                if (!completed)
                {
                    result.error = GetLastError();
                    result.content.clear();
                    return result;
                }

                if (bytes_read != bytes_to_read)
                {
                    result.error = bytes_read > bytes_to_read
                        ? ERROR_INVALID_DATA
                        : ERROR_HANDLE_EOF;
                    result.content.clear();
                    return result;
                }

                const bool continue_reading = collector.consume(
                    std::string_view(buffer.data(), bytes_read)
                );

                if (!continue_reading)
                {
                    if (collector.overflowed())
                    {
                        result.error = ERROR_NOT_ENOUGH_MEMORY;
                        result.content.clear();
                    }

                    return result;
                }

                position += bytes_read;
            }

            return result;
        }
    }

    ReadResults read_file(const ReadRequests& requests)
    {
        ReadResults results;
        results.reserve(requests.size());

        for (const ReadRequest& request : requests)
            results.push_back(read_one(request));

        return results;
    }

    ReadResult read_file(
        const std::filesystem::path& path,
        std::uint64_t start_line,
        std::uint64_t end_line
    )
    {
        const ReadRequests requests{
            ReadRequest{path, start_line, end_line}
        };

        ReadResults results = read_file(requests);

        return results.empty()
            ? ReadResult{path, start_line, end_line}
            : std::move(results.front());
    }

    ReadResult read_file(const std::filesystem::path& path)
    {
        return read_file(
            path,
            1,
            std::numeric_limits<std::uint64_t>::max()
        );
    }
}
