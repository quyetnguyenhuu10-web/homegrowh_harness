#pragma once

#include <cstdint>

#include <nlohmann/json_fwd.hpp>

namespace context_usage
{
    std::uint64_t estimate(const nlohmann::json& body);

    std::uint64_t openai(
        std::uint64_t prompt_tokens,
        std::uint64_t completion_tokens);

    std::uint64_t deepseek(
        std::uint64_t prompt_cache_hit_tokens,
        std::uint64_t prompt_cache_miss_tokens,
        std::uint64_t completion_tokens);

    std::uint64_t bonsai(
        std::uint64_t cache_n,
        std::uint64_t prompt_n,
        std::uint64_t predicted_n);
}
