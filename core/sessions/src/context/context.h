#pragma once

#include <cstdint>

#include <nlohmann/json_fwd.hpp>
#include <provider>

namespace sessions::detail
{
    std::uint64_t checked_add(
        std::uint64_t left,
        std::uint64_t right);

    std::uint64_t tool_definition_estimate(
        const nlohmann::json& tool_definitions);

    std::uint64_t exact_context_usage(
        const provider::RequestUsage& usage);

    bool should_compact(
        std::uint64_t usage_checkpoint,
        std::uint64_t current_estimate,
        std::uint64_t compact_threshold);
}
