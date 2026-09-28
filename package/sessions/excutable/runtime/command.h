#pragma once

#include <cstdint>
#include <vector>

#include <nlohmann/json.hpp>

namespace sessions_runtime
{
    enum class CommandOpcode : std::uint8_t
    {
        register_session = 1,
        declare_request = 2,
        run_request = 3,
        declare_response = 4,
        run_response = 5,
        declare_tool = 6,
        run_tool = 7,
        close = 8,
    };

    struct Command final
    {
        std::uint64_t id = 0;
        CommandOpcode opcode = CommandOpcode::register_session;
        nlohmann::json payload = nullptr;
    };

    Command decode_command(const std::vector<std::uint8_t>& bytes);
    const char* command_name(CommandOpcode opcode) noexcept;
}
