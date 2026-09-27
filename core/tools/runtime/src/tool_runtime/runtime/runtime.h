#pragma once

#include "../execute/execute.h"

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace tool_runtime::detail
{
    nlohmann::json execute_tool(
        const nlohmann::json& tool_call,
        std::vector<std::string>& read_files,
        std::uint32_t timeout_ms);
}
