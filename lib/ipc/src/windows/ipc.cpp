#include <ipc>

#include "io.h"
#include "resource.h"

#include <limits>
#include <mutex>
#include <utility>

namespace ipc
{
    namespace
    {
        constexpr DWORD pipe_buffer_size = 64 * 1024;

        struct path_result
        {
            std::wstring wide;
            std::string value;
            std::optional<Error> error;
        };

        path_result pipe_path(const std::string& name, std::string_view operation)
        {
            if (auto error = detail::validate_name(name, operation))
                return {{}, {}, std::move(error)};
            if (name.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                return {{}, {}, detail::make_error(operation, "validation_error",
                    "Endpoint name exceeds UTF-8 conversion capacity", {{"size", name.size()}})};

            const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                name.data(), static_cast<int>(name.size()), nullptr, 0);
            if (count == 0)
            {
                const DWORD code = GetLastError();
                // Keep even invalid UTF-8 losslessly serializable as bytes.
                return {{}, {}, detail::win32_error(operation, code, "MultiByteToWideChar",
                    {{"encoding", "utf-8"}, {"name_bytes", std::vector<std::uint8_t>(name.begin(), name.end())}})};
            }
            std::wstring wide(static_cast<std::size_t>(count), L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    name.data(), static_cast<int>(name.size()), wide.data(), count) == 0)
            {
                const DWORD code = GetLastError();
                return {{}, {}, detail::win32_error(operation, code, "MultiByteToWideChar", {{"name", name}})};
            }
            return {L"\\\\.\\pipe\\" + wide, "\\\\.\\pipe\\" + name, std::nullopt};
        }

        struct handle_result
        {
            detail::unique_handle value;
            std::optional<Error> error;
        };

        handle_result create_pipe(
            const std::wstring& path, const std::string& path_text,
            std::string_view operation, bool first)
        {
            const DWORD open_mode = PIPE_ACCESS_DUPLEX
                | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
            const HANDLE created = CreateNamedPipeW(path.c_str(), open_mode,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES, pipe_buffer_size, pipe_buffer_size, 0, nullptr);
            if (created == INVALID_HANDLE_VALUE)
            {
                const DWORD code = GetLastError();
                return {{}, detail::win32_error(operation, code, "CreateNamedPipeW", {{"path", path_text}})};
            }
            return {detail::unique_handle(created), std::nullopt};
        }

        void cleanup_handle(
            detail::unique_handle& handle, std::string_view operation,
            const std::string& path, std::optional<Error>& error)
        {
            const DWORD code = handle.close();
            if (code != ERROR_SUCCESS)
                detail::add_cleanup_error(error,
                    detail::win32_error(operation, code, "CloseHandle", {{"path", path}}));
        }
    }

    struct server::impl final
    {
        impl(std::wstring&& path_value, std::string&& path_text_value,
            detail::unique_handle&& pending_value) noexcept
            : path(std::move(path_value)), path_text(std::move(path_text_value)),
              pending(std::move(pending_value)) {}

        std::wstring path;
        std::string path_text;
        detail::unique_handle pending;
    };

    struct connection::impl final
    {
        impl(detail::unique_handle&& handle_value, std::string&& path_value,
            bool overlapped_value) noexcept
            : handle(std::move(handle_value)), path(std::move(path_value)),
              overlapped(overlapped_value) {}

        detail::unique_handle handle;
        std::string path;
        bool overlapped = false;
        std::mutex read_mutex;
        std::mutex write_mutex;
    };

    server::server() noexcept = default;
    server::server(std::unique_ptr<impl>&& implementation) noexcept : impl_(std::move(implementation)) {}
    server::server(server&&) noexcept = default;
    server& server::operator=(server&&) noexcept = default;
    server::~server() = default;
    connection::connection() noexcept = default;
    connection::connection(std::unique_ptr<impl>&& implementation) noexcept : impl_(std::move(implementation)) {}
    connection::connection(connection&&) noexcept = default;
    connection& connection::operator=(connection&&) noexcept = default;
    connection::~connection() = default;

    server_result listen(const std::string& name)
    {
        server_result result;
        detail::unique_handle pending;
        path_result path;
        try
        {
            path = pipe_path(name, "listen");
            if (path.error)
                return {{}, std::move(path.error)};
            auto created = create_pipe(path.wide, path.value, "listen", true);
            if (created.error)
                return {{}, std::move(created.error)};
            pending = std::move(created.value);
            result.value = server(std::make_unique<server::impl>(
                std::move(path.wide), std::move(path.value), std::move(pending)));
        }
        catch (const std::exception& exception)
        {
            result.error = detail::make_exception_error("listen", exception, {{"name", name}, {"path", path.value}});
            cleanup_handle(pending, "listen", path.value, result.error);
        }
        return result;
    }

    connection_result accept(server& listener)
    {
        if (!listener.impl_)
            return {{}, detail::make_error("accept", "validation_error", "Listener has no endpoint")};
        connection_result result;
        detail::unique_handle accepted;
        const std::string& path = listener.impl_->path_text;
        try
        {
            if (!listener.impl_->pending.valid())
            {
                auto created = create_pipe(listener.impl_->path, path, "accept", false);
                if (created.error)
                    return {{}, std::move(created.error)};
                listener.impl_->pending = std::move(created.value);
            }
            if (!ConnectNamedPipe(listener.impl_->pending.get(), nullptr))
            {
                const DWORD code = GetLastError();
                if (code != ERROR_PIPE_CONNECTED)
                    return {{}, detail::win32_error("accept", code, "ConnectNamedPipe", {{"path", path}})};
            }
            accepted = std::move(listener.impl_->pending);
            std::string connection_path = path;
            result.value = connection(std::make_unique<connection::impl>(
                std::move(accepted), std::move(connection_path), false));
        }
        catch (const std::exception& exception)
        {
            result.error = detail::make_exception_error("accept", exception, {{"path", path}});
            cleanup_handle(accepted, "accept", path, result.error);
        }
        return result;
    }

    connection_result connect(const std::string& name)
    {
        connection_result result;
        detail::unique_handle handle;
        path_result path;
        try
        {
            path = pipe_path(name, "connect");
            if (path.error)
                return {{}, std::move(path.error)};
            const HANDLE created = CreateFileW(path.wide.c_str(),
                GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
            if (created == INVALID_HANDLE_VALUE)
            {
                const DWORD code = GetLastError();
                return {{}, detail::win32_error("connect", code, "CreateFileW", {{"path", path.value}})};
            }
            handle = detail::unique_handle(created);
            result.value = connection(std::make_unique<connection::impl>(
                std::move(handle), std::move(path.value), true));
        }
        catch (const std::exception& exception)
        {
            result.error = detail::make_exception_error("connect", exception, {{"name", name}, {"path", path.value}});
            cleanup_handle(handle, "connect", path.value, result.error);
        }
        return result;
    }

    write_result write(connection& target, std::span<const std::uint8_t> data)
    {
        if (!target.impl_)
            return {detail::make_error("write", "validation_error", "Connection has no endpoint")};
        try
        {
            std::lock_guard lock(target.impl_->write_mutex);
            return detail::write_frame(data, target.impl_->path,
                [&](const void* input, std::size_t size, std::string_view phase)
                {
                    return detail::write_exact(target.impl_->handle.get(), input, size,
                        target.impl_->overlapped, target.impl_->path, phase);
                });
        }
        catch (const std::exception& exception)
        {
            return {detail::make_exception_error("write", exception, {{"path", target.impl_->path}})};
        }
    }

    read_result read(connection& source)
    {
        if (!source.impl_)
            return {{}, false, detail::make_error("read", "validation_error", "Connection has no endpoint")};
        try
        {
            std::lock_guard lock(source.impl_->read_mutex);
            return detail::read_frame(source.impl_->path,
                [&](void* output, std::size_t size, std::string_view phase)
                {
                    return detail::read_exact(source.impl_->handle.get(), output, size,
                        source.impl_->overlapped, source.impl_->path, phase);
                });
        }
        catch (const std::exception& exception)
        {
            return {{}, false, detail::make_exception_error("read", exception, {{"path", source.impl_->path}})};
        }
    }
}
