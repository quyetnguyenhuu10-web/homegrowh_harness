#include "source_stream.h"

namespace fsystem::windows::detail
{
    bool source_file::open(
        const std::filesystem::path& path,
        std::uint32_t& error
    )
    {
        handle_.reset();

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
            error = GetLastError();
            return false;
        }

        handle_.reset(raw_handle);

        LARGE_INTEGER file_size{};

        if (!GetFileSizeEx(handle_.get(), &file_size))
        {
            error = GetLastError();
            handle_.reset();
            return false;
        }

        if (file_size.QuadPart < 0)
        {
            error = ERROR_INVALID_DATA;
            handle_.reset();
            return false;
        }

        size_ = static_cast<std::uint64_t>(file_size.QuadPart);
        return true;
    }

    void source_file::reset() noexcept
    {
        handle_.reset();
        size_ = 0;
    }

    std::uint64_t source_file::size() const noexcept
    {
        return size_;
    }

    bool source_file::rewind(std::uint32_t& error)
    {
        LARGE_INTEGER origin{};

        if (!SetFilePointerEx(
                handle_.get(),
                origin,
                nullptr,
                FILE_BEGIN
            ))
        {
            error = GetLastError();
            return false;
        }

        return true;
    }

    bool source_file::read_chunk(
        char* buffer,
        std::size_t requested,
        std::size_t& filled,
        watcher_state& watcher_state,
        std::uint32_t& error
    )
    {
        filled = 0;

        while (filled < requested)
        {
            if (cancellation_requested(watcher_state))
                return false;

            DWORD bytes_read = 0;

            if (!ReadFile(
                    handle_.get(),
                    buffer + filled,
                    static_cast<DWORD>(requested - filled),
                    &bytes_read,
                    nullptr
                ))
            {
                error = GetLastError();
                return false;
            }

            if (bytes_read == 0)
            {
                error = ERROR_HANDLE_EOF;
                return false;
            }

            filled += bytes_read;

            if (cancellation_requested(watcher_state))
                return false;
        }

        return true;
    }
}
