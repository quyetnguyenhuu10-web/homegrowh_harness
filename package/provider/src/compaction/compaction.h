#pragma once

#include "request/requests.h"

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace provider
{
    struct CompactionResponse
    {
        RequestUsage usage = UsageState::unavailable;
    };

    struct CompactionResult
    {
        nlohmann::json messages;
        RequestUsage usage = UsageState::unavailable;
    };

    CompactionResult compaction(
        Provider selected_provider,
        const std::string& endpoint,
        const std::string& model_id,
        std::string_view api_key,
        std::string_view compaction_prompt,
        const nlohmann::json& session_current,
        const nlohmann::json& tool_definitions,
        const nlohmann::json& history = {},
        EventSink response_sink = {},
        bool compact = false,
        EventSink summary_sink = {},
        CompactionResponse* compaction_response = nullptr);
}
