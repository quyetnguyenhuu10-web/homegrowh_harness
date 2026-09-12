#include "windows_reader.h"

#include "../../../../config/reader_config.h"

#include <Windows.h>
#include <memory>
#include <span>

namespace file::windows
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
    }

    ReadResult read_file(std::filesystem::path path)
    {
        ReadResult result{};

        constexpr std::uint32_t block_size = file::config::kReadBlockSize;

        HANDLE raw_handle = CreateFileW(
            path.c_str(),
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

        const std::uintmax_t file_size =
            static_cast<std::uintmax_t>(file_size_info.QuadPart);

        if (file_size > static_cast<std::uintmax_t>(result.content.max_size()))
        {
            result.error = ERROR_NOT_ENOUGH_MEMORY;
            return result;
        }

        result.content.resize(static_cast<std::size_t>(file_size));

        if (file_size == 0)
            return result;

        DWORD bytesRead = 0;
        std::uint64_t position = 0;
        OVERLAPPED overlapped{};

        while (position < file_size)
        {
            const std::uint64_t remaining = file_size - position;
            const DWORD bytesToRead = static_cast<DWORD>(
                remaining < block_size ? remaining : block_size
            );

            std::span<char> destination(
                result.content.data() + static_cast<std::size_t>(position),
                bytesToRead
            );

            overlapped.Offset =
                static_cast<DWORD>(position % (1ULL << 32));

            overlapped.OffsetHigh =
                static_cast<DWORD>(position / (1ULL << 32));

            bytesRead = 0;

            BOOL success = ReadFile(
                hFile,
                destination.data(),
                bytesToRead,
                nullptr,
                &overlapped
            );

            if (!success)
            {
                const DWORD error = GetLastError();

                if (error == ERROR_HANDLE_EOF)
                {
                    result.error = error;
                    result.content.clear();
                    return result;
                }

                if (error != ERROR_IO_PENDING)
                {
                    result.error = error;
                    result.content.clear();
                    return result;
                }
            }

            BOOL resultOverlapped = GetOverlappedResult(
                hFile,
                &overlapped,
                &bytesRead,
                TRUE   // Tham số này để chờ đọc xong block hiện tại.
            );

            if (!resultOverlapped)   // Nếu trả về FALSE - không hoàn thành trọn vẹn nên cần check lỗi.
            {
                const DWORD error = GetLastError();

                if (error == ERROR_HANDLE_EOF)
                {
                    result.error = error;
                    result.content.clear();
                    return result;
                }

                result.error = error;
                result.content.clear();
                return result;
            }

            if (bytesRead > bytesToRead)
            {
                result.error = ERROR_INVALID_DATA;
                result.content.clear();
                return result;
            }

            position += bytesRead;

            if (bytesRead < bytesToRead)
            {
                result.error = ERROR_HANDLE_EOF;
                result.content.clear();
                return result;
            }
        }

        return result;
    }
}
