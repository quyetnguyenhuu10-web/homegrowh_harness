#pragma once

#include <cstdint>
#include <filesystem>

#include <nlohmann/json_fwd.hpp>

namespace tool_runtime::detail
{
    nlohmann::json execute_tool(
        const nlohmann::json& tool_call,
        const std::filesystem::path& workspace_path,
        bool refresh,
        std::uint32_t timeout_ms);
}
