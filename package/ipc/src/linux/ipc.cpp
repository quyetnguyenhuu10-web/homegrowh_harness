#include <ipc>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <utility>

namespace ipc
{
    namespace
    {
        constexpr std::uint32_t max_message_size = 16 * 1024 * 1024;

        std::error_code errno_error(int error) noexcept
        {
            return std::error_code(error, std::generic_category());
        }

        std::error_code validate_name(const std::string& name) noexcept
        {
            if (name.empty()
                || name.find('\0') != std::string::npos
                || name.find('/') != std::string::npos
                || name.find('\\') != std::string::npos)
            {
                return std::make_error_code(std::errc::invalid_argument);
            }
            return {};
        }

        struct path_result
        {
            std::string value;
            std::error_code error;
        };

        path_result socket_path(const std::string& name)
        {
            path_result result;
            result.error = validate_name(name);
            if (result.error)
                return result;

            result.value = "/tmp/" + name + ".sock";
            sockaddr_un probe{};
            if (result.value.size() >= sizeof(probe.sun_path))
            {
                result.value.clear();
                result.error = std::make_error_code(std::errc::filename_too_long);
            }
            return result;
        }

        struct fd_result
        {
            int value = -1;
            std::error_code error;
        };

        fd_result create_socket() noexcept
        {
            const int descriptor = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
            if (descriptor < 0)
                return {-1, errno_error(errno)};
            return {descriptor, {}};
        }

        void close_fd(int& descriptor) noexcept
        {
            if (descriptor >= 0)
                close(descriptor);
            descriptor = -1;
        }

        sockaddr_un make_address(const std::string& path) noexcept
        {
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
            return address;
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

        struct exact_read_result
        {
            bool closed = false;
            std::error_code error;
        };

        exact_read_result read_exact(
            int descriptor,
            void* output,
            std::size_t size) noexcept
        {
            auto* bytes = static_cast<unsigned char*>(output);
            std::size_t offset = 0;
            while (offset < size)
            {
                const ssize_t received = recv(
                    descriptor,
                    bytes + offset,
                    size - offset,
                    0);
                if (received > 0)
                {
                    offset += static_cast<std::size_t>(received);
                    continue;
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
                if (errno == EINTR)
                    continue;
                return {false, errno_error(errno)};
            }
            return {};
        }

        std::error_code write_exact(
            int descriptor,
            const void* input,
            std::size_t size) noexcept
        {
            const auto* bytes = static_cast<const unsigned char*>(input);
            std::size_t offset = 0;
            while (offset < size)
            {
                const ssize_t written = send(
                    descriptor,
                    bytes + offset,
                    size - offset,
                    MSG_NOSIGNAL);
                if (written > 0)
                {
                    offset += static_cast<std::size_t>(written);
                    continue;
                }
                if (written < 0 && errno == EINTR)
                    continue;
                if (written == 0)
                    return std::make_error_code(std::errc::io_error);
                return errno_error(errno);
            }
            return {};
        }
    }

    struct server::impl final
    {
        impl(int value, std::string socket_path_value)
            : descriptor(value), path(std::move(socket_path_value))
        {
        }

        ~impl()
        {
            close_fd(descriptor);
            if (!path.empty())
                unlink(path.c_str());
        }

        int descriptor = -1;
        std::string path;
    };

    struct connection::impl final
    {
        explicit impl(int value) noexcept : descriptor(value) {}

        ~impl()
        {
            close_fd(descriptor);
        }

        int descriptor = -1;
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
        path_result path = socket_path(name);
        if (path.error)
        {
            result.error = path.error;
            return result;
        }

        fd_result created = create_socket();
        if (created.error)
        {
            result.error = created.error;
            return result;
        }

        int descriptor = created.value;
        const sockaddr_un address = make_address(path.value);
        if (bind(
                descriptor,
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address)) != 0)
        {
            result.error = errno_error(errno);
            close_fd(descriptor);
            return result;
        }

        if (::listen(descriptor, SOMAXCONN) != 0)
        {
            result.error = errno_error(errno);
            close_fd(descriptor);
            unlink(path.value.c_str());
            return result;
        }

        result.value = server(std::make_unique<server::impl>(
            descriptor,
            std::move(path.value)));
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

        int descriptor = -1;
        do
        {
            descriptor = accept4(
                listener.impl_->descriptor,
                nullptr,
                nullptr,
                SOCK_CLOEXEC);
        }
        while (descriptor < 0 && errno == EINTR);

        if (descriptor < 0)
        {
            result.error = errno_error(errno);
            return result;
        }

        result.value = connection(
            std::make_unique<connection::impl>(descriptor));
        return result;
    }

    connection_result connect(std::string name)
    {
        connection_result result;
        path_result path = socket_path(name);
        if (path.error)
        {
            result.error = path.error;
            return result;
        }

        fd_result created = create_socket();
        if (created.error)
        {
            result.error = created.error;
            return result;
        }

        int descriptor = created.value;
        const sockaddr_un address = make_address(path.value);
        int status = -1;
        do
        {
            status = ::connect(
                descriptor,
                reinterpret_cast<const sockaddr*>(&address),
                sizeof(address));
        }
        while (status != 0 && errno == EINTR);

        if (status != 0)
        {
            result.error = errno_error(errno);
            close_fd(descriptor);
            return result;
        }

        result.value = connection(
            std::make_unique<connection::impl>(descriptor));
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
            target.impl_->descriptor,
            header.data(),
            header.size());
        if (result.error || data.empty())
            return result;

        result.error = write_exact(
            target.impl_->descriptor,
            data.data(),
            data.size());
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
            source.impl_->descriptor,
            header.data(),
            header.size());
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
            source.impl_->descriptor,
            result.data.data(),
            result.data.size());
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
