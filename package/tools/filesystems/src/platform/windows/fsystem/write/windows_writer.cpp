#include "windows_writer.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

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

        WriteResult write_one(
            const std::filesystem::path& path,
            const std::string& new_content
        )
        {
            WriteResult result{};
            result.path = path;

            HANDLE raw_handle = CreateFileW(
                path.c_str(),
                GENERIC_WRITE,
                0,
                nullptr,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                nullptr
            );

            if (raw_handle == INVALID_HANDLE_VALUE)
            {
                result.error = GetLastError();
                return result;
            }

            unique_handle handle(raw_handle);
            HANDLE file = handle.get();

            std::size_t offset = 0;

            while (offset < new_content.size())
            {
                const std::size_t remaining = new_content.size() - offset;
                const DWORD chunk_size = static_cast<DWORD>(
                    std::min<std::size_t>(
                        remaining,
                        static_cast<std::size_t>(
                            (std::numeric_limits<DWORD>::max)()
                        )
                    )
                );

                DWORD bytes_written = 0;
                const BOOL success = WriteFile(
                    file,
                    new_content.data() + offset,
                    chunk_size,
                    &bytes_written,
                    nullptr
                );

                if (!success)
                {
                    result.error = GetLastError();
                    return result;
                }

                if (bytes_written == 0 || bytes_written > chunk_size)
                {
                    result.error = ERROR_WRITE_FAULT;
                    return result;
                }

                offset += bytes_written;
            }

            if (!FlushFileBuffers(file))
                result.error = GetLastError();

            return result;
        }
    }

    WriteResults write_file(const WriteRequests& requests)
    {
        WriteResults results;
        results.reserve(requests.size());

        for (const WriteRequest& request : requests)
            results.push_back(write_one(request.path, request.new_content));

        return results;
    }

    WriteResult write_file(
        const std::filesystem::path& path,
        const std::string& new_content
    )
    {
        return write_one(path, new_content);
    }
}
