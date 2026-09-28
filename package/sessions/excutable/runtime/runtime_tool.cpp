#include "runtime.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace sessions_runtime
{
    nlohmann::json Runtime::declare_tool()
    {
        require_session();
        require_no_pending_stage();

        tool_stage_.emplace(
            sessions::declare_tool(*session_));

        const sessions::ToolStage& stage = *tool_stage_;
        return nlohmann::json{
            {"call_id", std::string(stage.call_id())},
            {"name", std::string(stage.name())},
            {"arguments", std::string(stage.arguments())},
            {"raw_call", stage.raw_call()}
        };
    }

    nlohmann::json Runtime::run_tool()
    {
        require_session();
        if (!tool_stage_.has_value())
            throw std::logic_error("no declared tool stage");

        sessions::ToolStage stage =
            std::move(*tool_stage_);
        tool_stage_.reset();
        sessions::run_tool(*session_, std::move(stage));
        return nlohmann::json::object();
    }
}
