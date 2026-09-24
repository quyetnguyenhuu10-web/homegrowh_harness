#pragma once

#include "request/requests.h"

#include <string>

#include <nlohmann/json.hpp>

namespace provider
{
    struct CompactionResult
    {
        nlohmann::json messages;
        RequestUsage usage = UsageState::unavailable;
    };

    CompactionResult compaction(
        const std::string& model_id,
        const nlohmann::json& messages,
        RawResponse* raw_response = nullptr,
        bool compact = false,
        const nlohmann::json& current_sessions = {},
        const nlohmann::json& tools = {});
}
