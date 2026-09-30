#include "io.h"

#include <sys/socket.h>

#include <cerrno>

namespace ipc::detail
{
    exact_read_result read_exact(
        int descriptor, void* output, std::size_t size,
        const std::string& path, std::string_view phase)
    {
        auto* bytes = static_cast<unsigned char*>(output);
        std::size_t offset = 0;
        while (offset < size)
        {
            const ssize_t received = ::recv(descriptor, bytes + offset, size - offset, 0);
            if (received > 0)
            {
                offset += static_cast<std::size_t>(received);
                continue;
            }
            if (received == 0)
            {
                if (offset == 0 && phase == "header")
                    return {true, std::nullopt};
                return {false, incomplete_frame("recv", path, phase, size, offset)};
            }
            const int code = errno;
            if (code == EINTR)
                continue;
            return {false, make_system_error("read",
                std::error_code(code, std::generic_category()), "recv",
                {{"path", path}, {"phase", phase},
                 {"expected_size", size}, {"transferred", offset}})};
        }
        return {};
    }

    std::optional<Error> write_exact(
        int descriptor, const void* input, std::size_t size,
        const std::string& path, std::string_view phase)
    {
        const auto* bytes = static_cast<const unsigned char*>(input);
        std::size_t offset = 0;
        while (offset < size)
        {
            const ssize_t written = ::send(
                descriptor, bytes + offset, size - offset, MSG_NOSIGNAL);
            if (written > 0)
            {
                offset += static_cast<std::size_t>(written);
                continue;
            }
            if (written == 0)
            {
                return make_error("write", "protocol_error",
                    "send returned zero before the frame was complete",
                    {{"api", "send"}, {"path", path}, {"phase", phase},
                     {"expected_size", size}, {"transferred", offset}, {"return_value", written}});
            }
            const int code = errno;
            if (code == EINTR)
                continue;
            return make_system_error("write",
                std::error_code(code, std::generic_category()), "send",
                {{"path", path}, {"phase", phase},
                 {"expected_size", size}, {"transferred", offset}});
        }
        return std::nullopt;
    }
}
