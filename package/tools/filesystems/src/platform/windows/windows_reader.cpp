#include "windows_reader.h"

#include <Windows.h>
#include <memory>
#include <vector>

namespace file::windows
{
    namespace
    {
        struct m_handle
        {
            using pointer = HANDLE;

            void operator()(HANDLE handle)
            {
                if (handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(handle);
                }
            }
        };
    }

    ReadResult read_file(
        std::filesystem::path path,
        std::uint32_t MAX_BYTES_READ
    )
    {
        ReadResult result{};

        std::unique_ptr<HANDLE, m_handle> handle(CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
            nullptr
        ));

        HANDLE hFile = handle.get();

        if (hFile == INVALID_HANDLE_VALUE)
        {
            result.error = GetLastError();
            return result;
        }

        std::string buffer(MAX_BYTES_READ, '\0');
        DWORD bytesRead = 0;
        std::uint64_t n = 0;
        OVERLAPPED overlapped{};

        while (true)   // Đọc đến khi gặp lỗi làm kết quả không đầy đủ thì dừng nên không bị âm thầm bỏ qua lỗi.
        {
            std::uint64_t position =
                static_cast<std::uint64_t>(MAX_BYTES_READ) * n;

            overlapped.Offset =
                static_cast<DWORD>(position % (1ULL << 32));

            overlapped.OffsetHigh =
                static_cast<DWORD>(position / (1ULL << 32));

            bytesRead = 0;

            BOOL success = ReadFile(
                hFile,
                buffer.data(),
                MAX_BYTES_READ,
                nullptr,
                &overlapped
            );

            if (!success && GetLastError() != ERROR_IO_PENDING) // != để loại trừ trạng thái đang chờ, không phải lỗi crash.
            {
                if (GetLastError() == ERROR_HANDLE_EOF)  // ERROR_HANDLE_EOF là đọc hết file, không phải lỗi cần xóa result.content.
                    break;

                result.error = GetLastError();
                result.content.clear();
                return result;
            }

            BOOL resultOverlapped = GetOverlappedResult(
                hFile,
                &overlapped,
                &bytesRead,
                TRUE   // Tham số này để chờ đọc xong block hiện tại.
            );

            if (!resultOverlapped)   // Nếu trả về FALSE - không hoàn thành trọn vẹn nên cần check lỗi.
            {
                if (GetLastError() == ERROR_HANDLE_EOF)
                    break;

                result.error = GetLastError();
                result.content.clear();
                return result;
            }

            if (bytesRead == 0)
                break;

            result.content.append(buffer.data(), bytesRead);

            if (bytesRead < MAX_BYTES_READ)
                break;

            n++;
        }

        return result;
    }
}