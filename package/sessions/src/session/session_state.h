#pragma once

#include <nlohmann/json_fwd.hpp>

namespace sessions::detail
{
    const nlohmann::json& current_messages(
        const nlohmann::json& session_current);

    bool has_tool_calls(const nlohmann::json& assistant);
}
