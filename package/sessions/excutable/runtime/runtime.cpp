#include "runtime.h"

#include "wire_json.h"

#include <stdexcept>
#include <utility>

namespace sessions_runtime
{
    bool Runtime::execute(Command&& command)
    {
        if (command.id <= last_command_id_)
        {
            emit_failed(
                command,
                std::make_exception_ptr(std::invalid_argument(
                    "command id must increase monotonically")));
            return false;
        }
        last_command_id_ = command.id;

        try
        {
            nlohmann::json result;

            switch (command.opcode)
            {
                case CommandOpcode::register_session:
                    result = register_session(std::move(command.payload));
                    break;
                case CommandOpcode::declare_request:
                    result = declare_request();
                    break;
                case CommandOpcode::run_request:
                    result = run_request();
                    break;
                case CommandOpcode::declare_response:
                    result = declare_response();
                    break;
                case CommandOpcode::run_response:
                    result = run_response();
                    break;
                case CommandOpcode::declare_tool:
                    result = declare_tool();
                    break;
                case CommandOpcode::run_tool:
                    result = run_tool();
                    break;
                case CommandOpcode::close:
                    result = close();
                    emit_finished(
                        command,
                        std::move(result),
                        "closed");
                    return true;
            }

            emit_finished(command, std::move(result));
            return false;
        }
        catch (...)
        {
            emit_failed(command, std::current_exception());
            return false;
        }
    }

    void Runtime::require_session() const
    {
        if (!session_.has_value())
            throw std::logic_error("session is not registered");
    }

    void Runtime::require_no_pending_stage() const
    {
        if (
            request_stage_.has_value() ||
            response_stage_.has_value() ||
            tool_stage_.has_value())
        {
            throw std::logic_error(
                "a declared stage is still pending");
        }
    }

    std::string Runtime::state_name() const
    {
        if (!session_.has_value())
            return "unregistered";
        return session_state_name(session_->state());
    }
}
