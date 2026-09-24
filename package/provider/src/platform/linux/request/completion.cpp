#include "completion.h"

#include <cerrno>
#include <climits>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>

namespace provider::linux
{
    namespace
    {
        struct Packet
        {
            detail::NativeCompletionType type;
            RawResponse* response;
            std::size_t bytes;
        };

        std::uintptr_t encode_fd(int fd) noexcept
        {
            return static_cast<std::uintptr_t>(fd) + 1;
        }

        int decode_fd(std::uintptr_t handle) noexcept
        {
            return static_cast<int>(handle - 1);
        }

        void write_packet(
            std::uintptr_t producer_handle,
            const Packet& packet)
        {
            if (producer_handle == 0)
            {
                return;
            }

            const int fd = decode_fd(producer_handle);
            const char* data = reinterpret_cast<const char*>(&packet);
            std::size_t remaining = sizeof(packet);

            while (remaining != 0)
            {
                const ssize_t written = ::write(fd, data, remaining);

                if (written < 0)
                {
                    const int error = errno;

                    if (error == EINTR)
                    {
                        continue;
                    }

                    throw std::system_error(
                        error,
                        std::generic_category());
                }

                data += written;
                remaining -= static_cast<std::size_t>(written);
            }
        }

        void read_packet(int fd, Packet* packet)
        {
            char* data = reinterpret_cast<char*>(packet);
            std::size_t remaining = sizeof(*packet);

            while (remaining != 0)
            {
                const ssize_t received = ::read(fd, data, remaining);

                if (received < 0)
                {
                    const int error = errno;

                    if (error == EINTR)
                    {
                        continue;
                    }

                    throw std::system_error(
                        error,
                        std::generic_category());
                }

                if (received == 0)
                {
                    throw std::runtime_error("provider completion pipe closed");
                }

                data += received;
                remaining -= static_cast<std::size_t>(received);
            }
        }
    }

    void completion_create(std::uintptr_t (&handles)[2])
    {
        int fds[2];

        if (::pipe(fds) != 0)
        {
            const int error = errno;

            throw std::system_error(
                error,
                std::generic_category());
        }

        handles[0] = encode_fd(fds[0]);
        handles[1] = encode_fd(fds[1]);
    }

    void completion_destroy(std::uintptr_t (&handles)[2]) noexcept
    {
        if (handles[0] != 0)
        {
            ::close(decode_fd(handles[0]));
        }

        if (handles[1] != 0)
        {
            ::close(decode_fd(handles[1]));
        }

        handles[0] = 0;
        handles[1] = 0;
    }

    std::uintptr_t completion_producer_handle(
        const std::uintptr_t (&handles)[2]) noexcept
    {
        return handles[1];
    }

    void completion_post_data(
        std::uintptr_t producer_handle,
        RawResponse* response,
        std::size_t bytes)
    {
        write_packet(
            producer_handle,
            Packet{detail::NativeCompletionType::data, response, bytes});
    }

    void completion_post_finished(
        std::uintptr_t producer_handle,
        RawResponse* response)
    {
        write_packet(
            producer_handle,
            Packet{detail::NativeCompletionType::finished, response, 0});
    }

    void completion_post_failed(
        std::uintptr_t producer_handle,
        RawResponse* response,
        std::uint32_t error)
    {
        write_packet(
            producer_handle,
            Packet{
                detail::NativeCompletionType::failed,
                response,
                error});
    }

    bool completion_wait(
        const std::uintptr_t (&handles)[2],
        detail::NativeCompletion* completion,
        std::uint32_t timeout_ms)
    {
        const int fd = decode_fd(handles[0]);
        pollfd descriptor{fd, POLLIN, 0};
        const int timeout = timeout_ms == 0xFFFFFFFFu
            ? -1
            : timeout_ms > static_cast<std::uint32_t>(INT_MAX)
                ? INT_MAX
                : static_cast<int>(timeout_ms);

        while (true)
        {
            const int result = ::poll(&descriptor, 1, timeout);

            if (result == 0)
            {
                return false;
            }

            if (result > 0)
            {
                break;
            }

            const int error = errno;

            if (error == EINTR)
            {
                continue;
            }

            completion->type = detail::NativeCompletionType::failed;
            completion->response = nullptr;
            completion->bytes = 0;
            completion->error = static_cast<std::uint32_t>(error);
            return true;
        }

        Packet packet{};

        try
        {
            read_packet(fd, &packet);
        }
        catch (const std::system_error& error)
        {
            completion->type = detail::NativeCompletionType::failed;
            completion->response = nullptr;
            completion->bytes = 0;
            completion->error = static_cast<std::uint32_t>(
                error.code().value());
            return true;
        }

        completion->type = packet.type;
        completion->response = packet.response;
        completion->bytes = packet.type == detail::NativeCompletionType::data
            ? packet.bytes
            : 0;
        completion->error = packet.type == detail::NativeCompletionType::failed
            ? static_cast<std::uint32_t>(packet.bytes)
            : 0;
        return true;
    }
}
