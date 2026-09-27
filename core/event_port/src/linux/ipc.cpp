#include "../ipc/platform.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace event_port::ipc::detail
{
    namespace
    {
        class unique_fd final
        {
        public:
            unique_fd() noexcept = default;

            explicit unique_fd(int fd) noexcept
                : fd_(fd)
            {
            }

            unique_fd(const unique_fd&) = delete;
            unique_fd& operator=(const unique_fd&) = delete;

            unique_fd(unique_fd&& other) noexcept
                : fd_(std::exchange(other.fd_, -1))
            {
            }

            unique_fd& operator=(unique_fd&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    fd_ = std::exchange(other.fd_, -1);
                }
                return *this;
            }

            ~unique_fd()
            {
                reset();
            }

            [[nodiscard]] int get() const noexcept
            {
                return fd_;
            }

            void reset() noexcept
            {
                if (fd_ >= 0)
                {
                    close(fd_);
                    fd_ = -1;
                }
            }

        private:
            int fd_ = -1;
        };

        std::error_code errno_code(int value) noexcept
        {
            return {value, std::generic_category()};
        }

        [[noreturn]] void throw_errno(const char* action)
        {
            throw std::system_error(errno_code(errno), action);
        }

        std::string make_endpoint()
        {
            static std::atomic<std::uint64_t> sequence{1};
            return "@homegrowh.event_port."
                + std::to_string(static_cast<unsigned long long>(getpid()))
                + "."
                + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        }

        sockaddr_un abstract_address(const std::string& endpoint, socklen_t& size)
        {
            if (endpoint.size() < 2 || endpoint.front() != '@')
                throw std::invalid_argument("event_port ipc abstract endpoint is invalid");

            const std::string_view name(endpoint.data() + 1, endpoint.size() - 1);
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            if (name.size() + 1 > sizeof(address.sun_path))
                throw std::length_error("event_port ipc abstract endpoint is too long");

            address.sun_path[0] = '\0';
            std::memcpy(address.sun_path + 1, name.data(), name.size());
            size = static_cast<socklen_t>(
                offsetof(sockaddr_un, sun_path) + 1 + name.size());
            return address;
        }

        class linux_connection final : public connection_backend
        {
        public:
            explicit linux_connection(unique_fd&& fd) noexcept
                : fd_(std::move(fd))
            {
            }

            std::size_t read(std::span<std::byte> buffer) override
            {
                if (buffer.empty())
                    return 0;

                for (;;)
                {
                    const ssize_t received = recv(
                        fd_.get(),
                        buffer.data(),
                        buffer.size(),
                        0);
                    if (received > 0)
                        return static_cast<std::size_t>(received);
                    if (received == 0)
                        return 0;
                    if (errno == EINTR)
                        continue;
                    throw_errno("recv(event_port ipc)");
                }
            }

            void write(std::span<const std::byte> data) override
            {
                std::size_t offset = 0;
                while (offset < data.size())
                {
                    const ssize_t written = send(
                        fd_.get(),
                        data.data() + offset,
                        data.size() - offset,
                        MSG_NOSIGNAL);
                    if (written > 0)
                    {
                        offset += static_cast<std::size_t>(written);
                        continue;
                    }
                    if (written < 0 && errno == EINTR)
                        continue;
                    if (written == 0)
                    {
                        throw std::system_error(
                            std::make_error_code(std::errc::io_error),
                            "send(event_port ipc) wrote zero bytes");
                    }
                    throw_errno("send(event_port ipc)");
                }
            }

        private:
            unique_fd fd_;
        };

        class linux_listener final : public listener_backend
        {
        public:
            linux_listener()
                : endpoint_(make_endpoint()),
                  listener_(socket(AF_UNIX, SOCK_STREAM, 0))
            {
                if (listener_.get() < 0)
                    throw_errno("socket(event_port ipc)");

                const int listener_flags = fcntl(listener_.get(), F_GETFD);
                if (listener_flags < 0
                    || fcntl(listener_.get(), F_SETFD, listener_flags | FD_CLOEXEC) != 0)
                {
                    throw_errno("fcntl(FD_CLOEXEC event_port ipc listener)");
                }

                socklen_t size = 0;
                const sockaddr_un address = abstract_address(endpoint_, size);
                if (bind(
                        listener_.get(),
                        reinterpret_cast<const sockaddr*>(&address),
                        size) != 0)
                {
                    throw_errno("bind(event_port ipc)");
                }
                if (listen(listener_.get(), SOMAXCONN) != 0)
                    throw_errno("listen(event_port ipc)");
            }

            const std::string& endpoint() const noexcept override
            {
                return endpoint_;
            }

            std::unique_ptr<connection_backend> accept() override
            {
                return accept_impl(-1);
            }

            std::unique_ptr<connection_backend> accept_for(
                std::chrono::milliseconds timeout) override
            {
                const auto count = timeout.count();
                const int wait_ms = count > static_cast<long long>((std::numeric_limits<int>::max)())
                    ? (std::numeric_limits<int>::max)()
                    : static_cast<int>(count);
                return accept_impl(wait_ms);
            }

        private:
            std::unique_ptr<connection_backend> accept_impl(int wait_ms)
            {
                for (;;)
                {
                    pollfd descriptor{};
                    descriptor.fd = listener_.get();
                    descriptor.events = POLLIN;
                    const int ready = poll(&descriptor, 1, wait_ms);
                    if (ready == 0)
                        return nullptr;
                    if (ready < 0)
                    {
                        if (errno == EINTR)
                            continue;
                        throw_errno("poll(event_port ipc accept)");
                    }

                    const int fd = accept(listener_.get(), nullptr, nullptr);
                    if (fd >= 0)
                    {
                        const int flags = fcntl(fd, F_GETFD);
                        if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0)
                        {
                            const int error = errno;
                            close(fd);
                            throw std::system_error(
                                errno_code(error),
                                "fcntl(FD_CLOEXEC event_port ipc connection)");
                        }
                        return std::make_unique<linux_connection>(unique_fd(fd));
                    }
                    if (errno == EINTR)
                        continue;
                    throw_errno("accept(event_port ipc)");
                }
            }
            std::string endpoint_;
            unique_fd listener_;
        };
    }

    std::unique_ptr<listener_backend> make_listener()
    {
        return std::make_unique<linux_listener>();
    }

    std::unique_ptr<connection_backend> connect(const std::string& endpoint)
    {
        unique_fd fd(socket(AF_UNIX, SOCK_STREAM, 0));
        if (fd.get() < 0)
            throw_errno("socket(event_port ipc connect)");

        const int flags = fcntl(fd.get(), F_GETFD);
        if (flags < 0 || fcntl(fd.get(), F_SETFD, flags | FD_CLOEXEC) != 0)
            throw_errno("fcntl(FD_CLOEXEC event_port ipc connect)");

        socklen_t size = 0;
        const sockaddr_un address = abstract_address(endpoint, size);
        for (;;)
        {
            if (::connect(
                    fd.get(),
                    reinterpret_cast<const sockaddr*>(&address),
                    size) == 0)
            {
                return std::make_unique<linux_connection>(std::move(fd));
            }
            if (errno == EINTR)
                continue;
            throw_errno("connect(event_port ipc)");
        }
    }
}
