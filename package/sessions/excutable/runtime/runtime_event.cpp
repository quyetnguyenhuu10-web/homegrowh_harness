#include "runtime.h"

#include "wire_json.h"

#include <event_port>

#include <cstdint>
#include <string>
#include <utility>

namespace sessions_runtime
{
    namespace
    {
        event_port::References command_references(
            std::uint64_t command_id)
        {
            event_port::References references;
            references.emplace_back(
                std::string("command_id"),
                std::to_string(command_id));
            return references;
        }

        void emit_runtime_event(
            event_port::Level level,
            std::string type,
            event_port::References references,
            nlohmann::json data)
        {
            event_port::port(event_port::Emit{
                "session_runtime",
                level,
                std::move(type),
                std::move(references),
                std::move(data)
            });
        }
    }

    void Runtime::emit_ready() const
    {
        emit_runtime_event(
            event_port::Level::info,
            "ready",
            {},
            nlohmann::json{{"protocol_version", 1}});
    }

    void Runtime::emit_protocol_error(
        std::exception_ptr error) const noexcept
    {
        try
        {
            emit_runtime_event(
                event_port::Level::error,
                "protocol_error",
                {},
                nlohmann::json{{"error", error_json(error)}});
        }
        catch (...)
        {
        }
    }

    void Runtime::emit_runtime_failure(
        std::string_view source,
        std::exception_ptr error,
        int exit_code) const noexcept
    {
        try
        {
            emit_runtime_event(
                event_port::Level::critical,
                "runtime_failed",
                {},
                nlohmann::json{
                    {"source", std::string(source)},
                    {"exit_code", exit_code},
                    {"error", error_json(error)}
                });
        }
        catch (...)
        {
        }
    }

    void Runtime::emit_finished(
        const Command& command,
        nlohmann::json&& result,
        const char* state_override) const
    {
        emit_runtime_event(
            event_port::Level::info,
            "command_finished",
            command_references(command.id),
            nlohmann::json{
                {"command_id", command.id},
                {"opcode", static_cast<std::uint8_t>(command.opcode)},
                {"command", command_name(command.opcode)},
                {"state",
                    state_override == nullptr
                        ? state_name()
                        : std::string(state_override)},
                {"result", std::move(result)}
            });
    }

    void Runtime::emit_failed(
        const Command& command,
        std::exception_ptr error) const noexcept
    {
        try
        {
            emit_runtime_event(
                event_port::Level::error,
                "command_failed",
                command_references(command.id),
                nlohmann::json{
                    {"command_id", command.id},
                    {"opcode",
                        static_cast<std::uint8_t>(command.opcode)},
                    {"command", command_name(command.opcode)},
                    {"state", state_name()},
                    {"error", error_json(error)}
                });
        }
        catch (...)
        {
        }
    }
}
