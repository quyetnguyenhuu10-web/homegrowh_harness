#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>
#include <provider>

namespace sessions
{
    provider::CompactionResult request(
        const std::string& api_key_signature,
        provider::Provider selected_provider,
        const std::string& endpoint,
        const std::string& model_id,
        std::string_view compaction_prompt,
        const nlohmann::json& session_current,
        const nlohmann::json& tool_definitions,
        bool compact,
        const nlohmann::json& history = {},
        provider::EventSink response_sink = {},
        provider::EventSink summary_sink = {},
        provider::CompactionResponse* compaction_response = nullptr);
}
