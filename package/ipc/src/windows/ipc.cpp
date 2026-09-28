#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <ipc>

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace ipc
{
    namespace
    {
        constexpr DWORD pipe_buffer_size = 64 * 1024;
        constexpr std::uint32_t max_message_size = 16 * 1024 * 1024;

        std::error_code win32_error(DWORD error) noexcept
        {
            return std::error_code(
                static_cast<int>(error),
                std::system_category());
        }

        std::error_code validate_name(const std::string& name) noexcept
        {
            if (name.empty()
                || name.find('\0') != std::string::npos
                || name.find('\\') != std::string::npos
                || name.find('/') != std::string::npos)
            {
                return std::make_error_code(std::errc::invalid_argument);
            }
            return {};
        }

        struct path_result
        {
            std::wstring value;
            std::error_code error;
        };

        path_result pipe_path(const std::string& name)
        {
            path_result result;
            result.error = validate_name(name);
            if (result.error)
                return result;

            const int count = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                name.data(),
                static_cast<int>(name.size()),
                nullptr,
                0);
            if (count == 0)
            {
                result.error = win32_error(GetLastError());
                return result;
            }

            std::wstring wide(static_cast<std::size_t>(count), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    name.data(),
                    static_cast<int>(name.size()),
                    wide.data(),
                    count) == 0)
            {
                result.error = win32_error(GetLastError());
                return result;
            }

            result.value = L"\\\\.\\pipe\\" + wide;
            return result;
        }

        struct handle_result
        {
            HANDLE value = INVALID_HANDLE_VALUE;
            std::error_code error;
        };

        handle_result create_pipe(const std::wstring& path, bool first) noexcept
        {
            const DWORD open_mode = PIPE_ACCESS_DUPLEX
                | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);

            const HANDLE handle = CreateNamedPipeW(
                path.c_str(),
                open_mode,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                pipe_buffer_size,
                pipe_buffer_size,
                0,
                nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                return {INVALID_HANDLE_VALUE, win32_error(GetLastError())};
            return {handle, {}};
        }

        void close_handle(HANDLE& handle) noexcept
        {
            if (handle != INVALID_HANDLE_VALUE)
                CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }

        std::array<std::uint8_t, 4> encode_size(std::uint32_t size) noexcept
        {
            std::array<std::uint8_t, 4> bytes{};
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                bytes[index] = static_cast<std::uint8_t>(
                    (size >> (index * 8)) & 0xffu);
            }
            return bytes;
        }

        std::uint32_t decode_size(const std::array<std::uint8_t, 4>& bytes) noexcept
        {
            std::uint32_t size = 0;
            for (std::size_t index = 0; index < bytes.size(); ++index)
                size |= static_cast<std::uint32_t>(bytes[index]) << (index * 8);
            return size;
        }

        bool peer_closed_error(DWORD error) noexcept
        {
            return error == ERROR_BROKEN_PIPE
                || error == ERROR_NO_DATA
                || error == ERROR_PIPE_NOT_CONNECTED;
        }

        struct exact_read_result
        {
            bool closed = false;
            std::error_code error;
        };

        struct event_handle final
        {
            event_handle() noexcept
                : value(CreateEventW(nullptr, TRUE, FALSE, nullptr))
            {
            }

            ~event_handle()
            {
                if (value != nullptr)
                    CloseHandle(value);
            }

            event_handle(const event_handle&) = delete;
            event_handle& operator=(const event_handle&) = delete;

            HANDLE value = nullptr;
        };

        exact_read_result read_exact_overlapped(
            HANDLE handle,
            void* output,
            std::size_t size) noexcept
        {
            auto* bytes = static_cast<unsigned char*>(output);
            std::size_t offset = 0;
            while (offset < size)
            {
                const std::size_t remaining = size - offset;
                const DWORD request = static_cast<DWORD>(
                    (std::min)(remaining,
                        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));

                event_handle event;
                if (event.value == nullptr)
                    return {false, win32_error(GetLastError())};

                OVERLAPPED overlapped{};
                overlapped.hEvent = event.value;

                const BOOL started = ReadFile(
                    handle,
                    bytes + offset,
                    request,
                    nullptr,
                    &overlapped);
                if (!started)
                {
                    const DWORD error = GetLastError();
                    if (peer_closed_error(error) && offset == 0)
                        return {true, {}};
                    if (error != ERROR_IO_PENDING)
                        return {false, win32_error(error)};
                }

                DWORD received = 0;
                if (!GetOverlappedResult(
                        handle,
                        &overlapped,
                        &received,
                        TRUE))
                {
                    const DWORD error = GetLastError();
                    if (peer_closed_error(error) && offset == 0)
                        return {true, {}};
                    return {false, win32_error(error)};
                }

                if (received == 0)
                {
                    if (offset == 0)
                        return {true, {}};
                    return {
                        false,
                        std::make_error_code(std::errc::protocol_error)
                    };
                }
                offset += received;
            }
            return {};
        }

        exact_read_result read_exact(
            HANDLE handle,
            void* output,
            std::size_t size,
            bool overlapped) noexcept
        {
            if (overlapped)
                return read_exact_overlapped(handle, output, size);

            auto* bytes = static_cast<unsigned char*>(output);
            std::size_t offset = 0;
            while (offset < size)
            {
                const std::size_t remaining = size - offset;
                const DWORD request = static_cast<DWORD>(
                    (std::min)(remaining,
                        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));

                DWORD received = 0;
                if (!ReadFile(handle, bytes + offset, request, &received, nullptr))
                {
                    const DWORD error = GetLastError();
                    if (peer_closed_error(error) && offset == 0)
                        return {true, {}};
                    return {false, win32_error(error)};
                }

                if (received == 0)
                {
                    if (offset == 0)
                        return {true, {}};
                    return {
                        false,
                        std::make_error_code(std::errc::protocol_error)
                    };
                }
                offset += received;
            }
            return {};
        }

        std::error_code write_exact_overlapped(
            HANDLE handle,
            const void* input,
            std::size_t size) noexcept
        {
            const auto* bytes = static_cast<const unsigned char*>(input);
            std::size_t offset = 0;
            while (offset < size)
            {
                const std::size_t remaining = size - offset;
                const DWORD request = static_cast<DWORD>(
                    (std::min)(remaining,
                        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));

                event_handle event;
                if (event.value == nullptr)
                    return win32_error(GetLastError());

                OVERLAPPED overlapped{};
                overlapped.hEvent = event.value;

                const BOOL started = WriteFile(
                    handle,
                    bytes + offset,
                    request,
                    nullptr,
                    &overlapped);
                if (!started)
                {
                    const DWORD error = GetLastError();
                    if (error != ERROR_IO_PENDING)
                        return win32_error(error);
                }

                DWORD written = 0;
                if (!GetOverlappedResult(
                        handle,
                        &overlapped,
                        &written,
                        TRUE))
                {
                    return win32_error(GetLastError());
                }
                if (written == 0)
                    return std::make_error_code(std::errc::io_error);
                offset += written;
            }
            return {};
        }

        std::error_code write_exact(
            HANDLE handle,
            const void* input,
            std::size_t size,
            bool overlapped) noexcept
        {
            if (overlapped)
                return write_exact_overlapped(handle, input, size);

            const auto* bytes = static_cast<const unsigned char*>(input);
            std::size_t offset = 0;
            while (offset < size)
            {
                const std::size_t remaining = size - offset;
                const DWORD request = static_cast<DWORD>(
                    (std::min)(remaining,
                        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));

                DWORD written = 0;
                if (!WriteFile(handle, bytes + offset, request, &written, nullptr))
                    return win32_error(GetLastError());
                if (written == 0)
                    return std::make_error_code(std::errc::io_error);
                offset += written;
            }
            return {};
        }
    }

    struct server::impl final
    {
        impl(std::wstring path_value, HANDLE pending_value) noexcept
            : path(std::move(path_value)), pending(pending_value)
        {
        }

        ~impl()
        {
            close_handle(pending);
        }

        std::wstring path;
        HANDLE pending = INVALID_HANDLE_VALUE;
    };

    struct connection::impl final
    {
        explicit impl(HANDLE value, bool overlapped_value) noexcept
            : handle(value), overlapped(overlapped_value)
        {
        }

        ~impl()
        {
            close_handle(handle);
        }

        HANDLE handle = INVALID_HANDLE_VALUE;
        bool overlapped = false;
        std::mutex read_mutex;
        std::mutex write_mutex;
    };

    server::server() noexcept = default;
    server::server(std::unique_ptr<impl>&& implementation) noexcept
        : impl_(std::move(implementation))
    {
    }
    server::server(server&&) noexcept = default;
    server& server::operator=(server&&) noexcept = default;
    server::~server() = default;

    connection::connection() noexcept = default;
    connection::connection(std::unique_ptr<impl>&& implementation) noexcept
        : impl_(std::move(implementation))
    {
    }
    connection::connection(connection&&) noexcept = default;
    connection& connection::operator=(connection&&) noexcept = default;
    connection::~connection() = default;

    server_result listen(std::string name)
    {
        server_result result;
        path_result path = pipe_path(name);
        if (path.error)
        {
            result.error = path.error;
            return result;
        }

        handle_result created = create_pipe(path.value, true);
        if (created.error)
        {
            result.error = created.error;
            return result;
        }

        result.value = server(std::make_unique<server::impl>(
            std::move(path.value),
            created.value));
        return result;
    }

    connection_result accept(server& listener)
    {
        connection_result result;
        if (!listener.impl_)
        {
            result.error = std::make_error_code(std::errc::invalid_argument);
            return result;
        }

        if (listener.impl_->pending == INVALID_HANDLE_VALUE)
        {
            handle_result created = create_pipe(listener.impl_->path, false);
            if (created.error)
            {
                result.error = created.error;
                return result;
            }
            listener.impl_->pending = created.value;
        }

        const HANDLE accepted = listener.impl_->pending;
        if (!ConnectNamedPipe(accepted, nullptr))
        {
            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED)
            {
                result.error = win32_error(error);
                return result;
            }
        }

        listener.impl_->pending = INVALID_HANDLE_VALUE;
        result.value = connection(
            std::make_unique<connection::impl>(accepted, false));
        return result;
    }

    connection_result connect(std::string name)
    {
        connection_result result;
        path_result path = pipe_path(name);
        if (path.error)
        {
            result.error = path.error;
            return result;
        }

        const HANDLE handle = CreateFileW(
            path.value.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            result.error = win32_error(GetLastError());
            return result;
        }

        result.value = connection(
            std::make_unique<connection::impl>(handle, true));
        return result;
    }

    write_result write(
        connection& target,
        std::span<const std::uint8_t> data)
    {
        write_result result;
        if (!target.impl_)
        {
            result.error = std::make_error_code(std::errc::invalid_argument);
            return result;
        }
        if (data.size() > max_message_size)
        {
            result.error = std::make_error_code(std::errc::message_size);
            return result;
        }

        std::lock_guard lock(target.impl_->write_mutex);
        const auto header = encode_size(static_cast<std::uint32_t>(data.size()));
        result.error = write_exact(
            target.impl_->handle,
            header.data(),
            header.size(),
            target.impl_->overlapped);
        if (result.error || data.empty())
            return result;

        result.error = write_exact(
            target.impl_->handle,
            data.data(),
            data.size(),
            target.impl_->overlapped);
        return result;
    }

    read_result read(connection& source)
    {
        read_result result;
        if (!source.impl_)
        {
            result.error = std::make_error_code(std::errc::invalid_argument);
            return result;
        }

        std::lock_guard lock(source.impl_->read_mutex);
        std::array<std::uint8_t, 4> header{};
        exact_read_result header_read = read_exact(
            source.impl_->handle,
            header.data(),
            header.size(),
            source.impl_->overlapped);
        if (header_read.closed)
        {
            result.closed = true;
            return result;
        }
        if (header_read.error)
        {
            result.error = header_read.error;
            return result;
        }

        const std::uint32_t size = decode_size(header);
        if (size > max_message_size)
        {
            result.error = std::make_error_code(std::errc::message_size);
            return result;
        }

        result.data.resize(size);
        if (result.data.empty())
            return result;

        exact_read_result payload_read = read_exact(
            source.impl_->handle,
            result.data.data(),
            result.data.size(),
            source.impl_->overlapped);
        if (payload_read.closed)
        {
            result.data.clear();
            result.error = std::make_error_code(std::errc::protocol_error);
            return result;
        }
        if (payload_read.error)
        {
            result.data.clear();
            result.error = payload_read.error;
        }
        return result;
    }
}
