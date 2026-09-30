#include "framing.h"

namespace ipc::detail
{
    std::array<std::uint8_t, 4> encode_size(std::uint32_t size) noexcept
    {
        std::array<std::uint8_t, 4> bytes{};
        for (std::size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<std::uint8_t>((size >> (index * 8)) & 0xffu);
        return bytes;
    }

    std::uint32_t decode_size(const std::array<std::uint8_t, 4>& bytes) noexcept
    {
        std::uint32_t size = 0;
        for (std::size_t index = 0; index < bytes.size(); ++index)
            size |= static_cast<std::uint32_t>(bytes[index]) << (index * 8);
        return size;
    }

    Error incomplete_frame(
        std::string_view api, const std::string& path, std::string_view phase,
        std::size_t expected, std::size_t transferred)
    {
        return make_error("read", "protocol_error",
            "Peer closed before the frame was complete",
            {{"api", api}, {"path", path}, {"phase", phase},
             {"expected_size", expected}, {"transferred", transferred}});
    }
}
