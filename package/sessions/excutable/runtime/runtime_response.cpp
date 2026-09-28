#include "runtime.h"

#include "wire_json.h"

#include <stdexcept>
#include <utility>

namespace sessions_runtime
{
    nlohmann::json Runtime::declare_response()
    {
        require_session();
        require_no_pending_stage();

        response_stage_.emplace(
            sessions::declare_response(*session_));

        const sessions::ResponseStage& stage = *response_stage_;
        return nlohmann::json{
            {"has_tool_calls", stage.has_tool_calls()},
            {"tool_count", stage.tool_count()},
            {"usage", usage_json(stage.usage())}
        };
    }

    nlohmann::json Runtime::run_response()
    {
        require_session();
        if (!response_stage_.has_value())
            throw std::logic_error("no declared response stage");

        sessions::ResponseStage stage =
            std::move(*response_stage_);
        response_stage_.reset();
        sessions::run_response(*session_, std::move(stage));
        return nlohmann::json::object();
    }
}
