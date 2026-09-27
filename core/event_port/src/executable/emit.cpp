#include "../ipc/channel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace
{
    std::array<std::byte, 4> encode_u32(std::uint32_t value) noexcept
    {
        return {
            static_cast<std::byte>(value & 0xffu),
            static_cast<std::byte>((value >> 8) & 0xffu),
            static_cast<std::byte>((value >> 16) & 0xffu),
            static_cast<std::byte>((value >> 24) & 0xffu),
        };
    }

    std::uint32_t decode_u32(const std::array<std::byte, 4>& bytes) noexcept
    {
        return static_cast<std::uint32_t>(bytes[0])
            | (static_cast<std::uint32_t>(bytes[1]) << 8)
            | (static_cast<std::uint32_t>(bytes[2]) << 16)
            | (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    void read_exact(event_port::ipc::Connection& connection, std::span<std::byte> output)
    {
        std::size_t offset = 0;
        while (offset < output.size())
        {
            const std::size_t count = connection.read(output.subspan(offset));
            if (count == 0)
                throw std::runtime_error("event_port_emit response ended early");
            offset += count;
        }
    }
}

int main()
{
    try
    {
        const char* endpoint = std::getenv("HOMEGROWPH_EVENT_PORT");
        if (endpoint == nullptr || *endpoint == '\0')
            throw std::runtime_error("HOMEGROWPH_EVENT_PORT is not set");

        std::ostringstream input_stream;
        input_stream << std::cin.rdbuf();
        if (std::cin.bad())
            throw std::runtime_error("event_port_emit failed to read stdin");
        const std::string input = input_stream.str();
        if (input.size() > (std::numeric_limits<std::uint32_t>::max)())
            throw std::length_error("event_port_emit input is too large");

        event_port::ipc::Connection connection = event_port::ipc::connect(endpoint);
        const auto size = encode_u32(static_cast<std::uint32_t>(input.size()));
        connection.write(std::span<const std::byte>{size.data(), size.size()});
        if (!input.empty())
        {
            connection.write(std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(input.data()),
                input.size()});
        }

        std::array<std::byte, 4> response_size_bytes{};
        read_exact(connection, response_size_bytes);
        const std::uint32_t response_size = decode_u32(response_size_bytes);
        std::string response(response_size, '\0');
        if (response_size != 0)
        {
            read_exact(connection, std::span<std::byte>{
                reinterpret_cast<std::byte*>(response.data()),
                response.size()});
        }

        const nlohmann::json ack = nlohmann::json::parse(response);
        if (!ack.value("ok", false))
            throw std::runtime_error(ack.value("error", std::string("event_port emit failed")));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what();
        return 2;
    }
}
