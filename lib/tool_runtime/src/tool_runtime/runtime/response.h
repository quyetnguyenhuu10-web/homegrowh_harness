#pragma once

#include <string_view>

#include <tool_runtime/tool_runtime.h>

namespace tool_runtime::detail
{
    Result<nlohmann::json> normalize_result_payload(
        nlohmann::json&& payload,
        std::string_view source,
        std::string_view operation);
    Result<nlohmann::json> normalize_result_message(
        const nlohmann::json& message,
        std::string_view source,
        std::string_view operation);
}
