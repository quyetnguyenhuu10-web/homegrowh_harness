#include "context.h"

#include <context_usage>

#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

#include <nlohmann/json.hpp>

namespace sessions::detail
{
    std::uint64_t checked_add(
        std::uint64_t left,
        std::uint64_t right)
    {
        if (right > (std::numeric_limits<std::uint64_t>::max)() - left)
            throw std::overflow_error("sessions loop context usage overflow");
        return left + right;
    }

    std::uint64_t tool_definition_estimate(
        const nlohmann::json& tool_definitions)
    {
        if (tool_definitions.empty())
            return 0;

        return context_usage::estimate(nlohmann::json{
            {"tools", tool_definitions}
        });
    }

    std::uint64_t exact_context_usage(
        const provider::RequestUsage& usage)
    {
        if (std::holds_alternative<provider::UsageState>(usage))
        {
            throw std::runtime_error(
                "sessions loop requires provider usage after each request");
        }

        return std::visit(
            [](const auto& value) -> std::uint64_t
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, provider::UsageState>)
                {
                    throw std::runtime_error(
                        "sessions loop requires provider usage after each request");
                }
                else if constexpr (std::is_same_v<T, provider::OpenAIUsage>)
                {
                    return context_usage::openai(
                        value.prompt_tokens,
                        value.completion_tokens);
                }
                else if constexpr (std::is_same_v<T, provider::DeepSeekUsage>)
                {
                    return context_usage::deepseek(
                        value.prompt_cache_hit_tokens,
                        value.prompt_cache_miss_tokens,
                        value.completion_tokens);
                }
                else
                {
                    return context_usage::bonsai(
                        value.cache_n,
                        value.prompt_n,
                        value.predicted_n);
                }
            },
            usage);
    }

    bool should_compact(
        std::uint64_t usage_checkpoint,
        std::uint64_t current_estimate,
        std::uint64_t compact_threshold)
    {
        const std::uint64_t total_usage =
            checked_add(usage_checkpoint, current_estimate);
        return total_usage > compact_threshold;
    }
}
