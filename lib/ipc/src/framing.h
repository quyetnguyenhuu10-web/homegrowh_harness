#pragma once

#include "error.h"

#include <array>
#include <cstddef>
#include <utility>

namespace ipc::detail
{
    // Both transports use a four-byte little-endian payload length.
    inline constexpr std::uint32_t max_message_size = 16 * 1024 * 1024;

    struct exact_read_result
    {
        bool closed = false;
        std::optional<Error> error;
    };

    std::array<std::uint8_t, 4> encode_size(std::uint32_t size) noexcept;
    std::uint32_t decode_size(const std::array<std::uint8_t, 4>& bytes) noexcept;

    Error incomplete_frame(
        std::string_view api,
        const std::string& path,
        std::string_view phase,
        std::size_t expected,
        std::size_t transferred);

    template <typename WriteExact>
    write_result write_frame(
        std::span<const std::uint8_t> data,
        const std::string& path,
        WriteExact&& write_exact)
    {
        if (data.size() > max_message_size)
        {
            return {make_error("write", "validation_error",
                "Payload exceeds the frame size limit",
                {{"path", path}, {"size", data.size()},
                 {"limit", max_message_size}})};
        }

        const auto header = encode_size(static_cast<std::uint32_t>(data.size()));
        auto error = write_exact(header.data(), header.size(), "header");
        if (error || data.empty())
            return {std::move(error)};
        return {write_exact(data.data(), data.size(), "payload")};
    }

    template <typename ReadExact>
    read_result read_frame(const std::string& path, ReadExact&& read_exact)
    {
        std::array<std::uint8_t, 4> header{};
        auto header_read = read_exact(header.data(), header.size(), "header");
        if (header_read.error)
            return {{}, false, std::move(header_read.error)};
        if (header_read.closed)
            return {{}, true, std::nullopt};

        const std::uint32_t size = decode_size(header);
        if (size > max_message_size)
        {
            return {{}, false, make_error("read", "protocol_error",
                "Frame length exceeds the frame size limit",
                {{"path", path}, {"size", size}, {"limit", max_message_size}})};
        }

        read_result result;
        result.data.resize(size);
        if (result.data.empty())
            return result;
        auto payload_read = read_exact(result.data.data(), size, "payload");
        if (payload_read.error)
        {
            result.data.clear();
            result.error = std::move(payload_read.error);
        }
        return result;
    }
}
