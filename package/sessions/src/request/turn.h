#pragma once

#include <stream/stream.h>

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <provider>

namespace sessions::detail
{
    struct TurnResult
    {
        provider::CompactionResult request;
        nlohmann::json assistant;
    };

    TurnResult run_turn(
        const std::string& api_key_signature,
        const std::string& endpoint,
        const std::string& model_id,
        provider::Provider selected_provider,
        std::string_view compaction_prompt,
        const nlohmann::json& session_current,
        const nlohmann::json& tool_definitions,
        bool compact,
        const nlohmann::json& history,
        const StreamCallback& stream,
        const EventLogCallback& event_log);
}
