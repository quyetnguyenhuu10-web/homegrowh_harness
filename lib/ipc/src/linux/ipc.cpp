#include <ipc>

#include "io.h"
#include "resource.h"

#include <sys/socket.h>
#include <sys/un.h>

#include <cstring>
#include <mutex>
#include <utility>

namespace ipc
{
    namespace
    {
        struct path_result
        {
            std::string value;
            std::optional<Error> error;
        };

        path_result socket_path(const std::string& name, std::string_view operation)
        {
            if (auto error = detail::validate_name(name, operation))
                return {{}, std::move(error)};
            std::string path = "/tmp/" + name + ".sock";
            sockaddr_un probe{};
            if (path.size() >= sizeof(probe.sun_path))
            {
                return {{}, detail::make_error(operation, "validation_error",
                    "Socket path exceeds sockaddr_un capacity",
                    {{"name", name}, {"path", path}, {"size", path.size()},
                     {"limit", sizeof(probe.sun_path) - 1}})};
            }
            return {std::move(path), std::nullopt};
        }

        sockaddr_un make_address(const std::string& path) noexcept
        {
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
            return address;
        }

        void cleanup_descriptor(
            detail::unique_fd& descriptor, std::string_view operation,
            const std::string& path, std::optional<Error>& error)
        {
            const int code = descriptor.close();
            if (code != 0)
                detail::add_cleanup_error(error, detail::make_system_error(operation,
                    std::error_code(code, std::generic_category()), "close", {{"path", path}}));
        }
    }

    struct server::impl final
    {
        impl(detail::owned_socket_path&& path_value, detail::unique_fd&& descriptor_value) noexcept
            : path(std::move(path_value)), descriptor(std::move(descriptor_value)) {}

        detail::owned_socket_path path;
        detail::unique_fd descriptor;
    };

    struct connection::impl final
    {
        impl(detail::unique_fd&& descriptor_value, std::string&& path_value) noexcept
            : descriptor(std::move(descriptor_value)), path(std::move(path_value)) {}

        detail::unique_fd descriptor;
        std::string path;
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
        detail::unique_fd descriptor;
        detail::owned_socket_path path;
        try
        {
            auto parsed = socket_path(name, "listen");
            if (parsed.error)
                return {{}, std::move(parsed.error)};
            path = detail::owned_socket_path(std::move(parsed.value));
            const int created = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (created < 0)
            {
                const int code = errno;
                return {{}, detail::make_system_error("listen",
                    std::error_code(code, std::generic_category()), "socket", {{"path", path.get()}})};
            }
            descriptor = detail::unique_fd(created);
            const auto address = make_address(path.get());
            if (::bind(descriptor.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
            {
                const int code = errno;
                result.error = detail::make_system_error("listen",
                    std::error_code(code, std::generic_category()), "bind", {{"path", path.get()}});
            }
            else
            {
                path.claim();
                if (::listen(descriptor.get(), SOMAXCONN) != 0)
                {
                    const int code = errno;
                    result.error = detail::make_system_error("listen",
                        std::error_code(code, std::generic_category()), "listen", {{"path", path.get()}});
                }
            }
            if (!result.error)
            {
                result.value = server(std::make_unique<server::impl>(std::move(path), std::move(descriptor)));
                return result;
            }
        }
        catch (const std::exception& exception)
        {
            result.error = detail::make_exception_error("listen", exception, {{"name", name}, {"path", path.get()}});
        }
        cleanup_descriptor(descriptor, "listen", path.get(), result.error);
        const int code = path.remove();
        if (code != 0)
            detail::add_cleanup_error(result.error, detail::make_system_error("listen",
                std::error_code(code, std::generic_category()), "unlink", {{"path", path.get()}}));
        return result;
    }

    connection_result accept(server& listener)
    {
        if (!listener.impl_)
            return {{}, detail::make_error("accept", "validation_error", "Listener has no endpoint")};
        connection_result result;
        detail::unique_fd descriptor;
        const std::string& path = listener.impl_->path.get();
        try
        {
            int accepted;
            int code;
            do
            {
                accepted = ::accept4(listener.impl_->descriptor.get(), nullptr, nullptr, SOCK_CLOEXEC);
                code = accepted < 0 ? errno : 0;
            } while (accepted < 0 && code == EINTR);
            if (accepted < 0)
                return {{}, detail::make_system_error("accept",
                    std::error_code(code, std::generic_category()), "accept4", {{"path", path}})};
            descriptor = detail::unique_fd(accepted);
            std::string connection_path = path;
            result.value = connection(std::make_unique<connection::impl>(std::move(descriptor), std::move(connection_path)));
        }
        catch (const std::exception& exception)
        {
            result.error = detail::make_exception_error("accept", exception, {{"path", path}});
            cleanup_descriptor(descriptor, "accept", path, result.error);
        }
        return result;
    }

    connection_result connect(const std::string& name)
    {
        connection_result result;
        detail::unique_fd descriptor;
        std::string path;
        try
        {
            auto parsed = socket_path(name, "connect");
            if (parsed.error)
                return {{}, std::move(parsed.error)};
            path = std::move(parsed.value);
            const int created = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (created < 0)
            {
                const int code = errno;
                return {{}, detail::make_system_error("connect",
                    std::error_code(code, std::generic_category()), "socket", {{"path", path}})};
            }
            descriptor = detail::unique_fd(created);
            const auto address = make_address(path);
            int status;
            int code;
            do
            {
                status = ::connect(descriptor.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
                code = status != 0 ? errno : 0;
            } while (status != 0 && code == EINTR);
            if (status != 0)
            {
                result.error = detail::make_system_error("connect",
                    std::error_code(code, std::generic_category()), "connect", {{"path", path}});
                cleanup_descriptor(descriptor, "connect", path, result.error);
                return result;
            }
            result.value = connection(std::make_unique<connection::impl>(std::move(descriptor), std::move(path)));
        }
        catch (const std::exception& exception)
        {
            result.error = detail::make_exception_error("connect", exception, {{"path", path}, {"name", name}});
            cleanup_descriptor(descriptor, "connect", path, result.error);
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
                { return detail::write_exact(target.impl_->descriptor.get(), input, size, target.impl_->path, phase); });
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
                { return detail::read_exact(source.impl_->descriptor.get(), output, size, source.impl_->path, phase); });
        }
        catch (const std::exception& exception)
        {
            return {{}, false, detail::make_exception_error("read", exception, {{"path", source.impl_->path}})};
        }
    }
}
