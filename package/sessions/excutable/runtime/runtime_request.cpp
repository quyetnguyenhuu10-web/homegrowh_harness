#include "runtime.h"

#include "wire_json.h"

#include <stdexcept>
#include <utility>

namespace sessions_runtime
{
    nlohmann::json Runtime::declare_request()
    {
        require_session();
        require_no_pending_stage();

        request_stage_.emplace(
            sessions::declare_request(*session_));

        const sessions::RequestStage& stage = *request_stage_;
        return nlohmann::json{
            {"compact", stage.compact()},
            {"context_usage", stage.context_usage()},
            {"context_limit", stage.context_limit()},
            {"model", stage.model()},
            {"provider", provider_name(stage.selected_provider())}
        };
    }

    nlohmann::json Runtime::run_request()
    {
        require_session();
        if (!request_stage_.has_value())
            throw std::logic_error("no declared request stage");

        sessions::RequestStage stage =
            std::move(*request_stage_);
        request_stage_.reset();
        sessions::run_request(*session_, std::move(stage));
        return nlohmann::json::object();
    }
}
