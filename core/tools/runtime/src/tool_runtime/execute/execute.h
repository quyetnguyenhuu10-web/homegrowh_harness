#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace tool_runtime
{
    nlohmann::json execute(
        const nlohmann::json& tool_call,
        std::vector<std::string>& read_files,
        std::uint32_t timeout_ms);
}
