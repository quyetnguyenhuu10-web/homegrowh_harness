#include "command.h"

#include <limits>
#include <stdexcept>

namespace sessions_runtime
{
    namespace
    {
        std::uint64_t unsigned_integer(
            const nlohmann::json& value,
            const char* field)
        {
            if (value.is_number_unsigned())
                return value.get<std::uint64_t>();

            if (value.is_number_integer())
            {
                const std::int64_t signed_value =
                    value.get<std::int64_t>();
                if (signed_value >= 0)
                    return static_cast<std::uint64_t>(signed_value);
            }

            throw std::invalid_argument(
                std::string(field) + " must be an unsigned integer");
        }

        CommandOpcode opcode_from_integer(std::uint64_t value)
        {
            if (
                value <
                    static_cast<std::uint64_t>(
                        CommandOpcode::register_session) ||
                value >
                    static_cast<std::uint64_t>(
                        CommandOpcode::close))
            {
                throw std::invalid_argument("command opcode is invalid");
            }

            return static_cast<CommandOpcode>(
                static_cast<std::uint8_t>(value));
        }
    }

    Command decode_command(const std::vector<std::uint8_t>& bytes)
    {
        if (bytes.empty())
            throw std::invalid_argument("command frame is empty");

        nlohmann::json decoded = nlohmann::json::from_cbor(bytes);
        if (!decoded.is_array())
            throw std::invalid_argument("command must be a CBOR array");
        if (decoded.size() < 2 || decoded.size() > 3)
        {
            throw std::invalid_argument(
                "command must contain 2 or 3 fields");
        }

        Command command;
        command.id = unsigned_integer(decoded.at(0), "command id");
        if (command.id == 0)
            throw std::invalid_argument("command id must be positive");

        command.opcode = opcode_from_integer(
            unsigned_integer(decoded.at(1), "command opcode"));

        if (command.opcode == CommandOpcode::register_session)
        {
            if (decoded.size() != 3)
            {
                throw std::invalid_argument(
                    "RegisterSession command requires config payload");
            }
            command.payload = std::move(decoded.at(2));
        }
        else if (decoded.size() != 2)
        {
            throw std::invalid_argument(
                "command opcode does not accept a payload");
        }

        return command;
    }

    const char* command_name(CommandOpcode opcode) noexcept
    {
        switch (opcode)
        {
            case CommandOpcode::register_session:
                return "register_session";
            case CommandOpcode::declare_request:
                return "declare_request";
            case CommandOpcode::run_request:
                return "run_request";
            case CommandOpcode::declare_response:
                return "declare_response";
            case CommandOpcode::run_response:
                return "run_response";
            case CommandOpcode::declare_tool:
                return "declare_tool";
            case CommandOpcode::run_tool:
                return "run_tool";
            case CommandOpcode::close:
                return "close";
        }
        return "unknown";
    }
}
