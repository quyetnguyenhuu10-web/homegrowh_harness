#include "runtime.h"

#include "config_parser.h"
#include "wire_json.h"

#include <stdexcept>
#include <utility>

namespace sessions_runtime
{
    nlohmann::json Runtime::register_session(
        nlohmann::json&& payload)
    {
        if (session_.has_value())
            throw std::logic_error("session is already registered");

        require_no_pending_stage();
        session_.emplace(sessions::register_session(
            parse_session_config(std::move(payload))));

        return nlohmann::json{
            {"state", state_name()}
        };
    }

    nlohmann::json Runtime::close()
    {
        require_session();

        request_stage_.reset();
        response_stage_.reset();
        tool_stage_.reset();

        sessions::SessionResult result =
            sessions::close_session(std::move(*session_));
        session_.reset();

        return nlohmann::json{
            {"history", std::move(result.history)},
            {"usage", usage_json(result.usage)}
        };
    }
}
