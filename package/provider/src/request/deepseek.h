#pragma once

#include <cstdint>
#include <optional>

namespace provider
{
    struct DeepSeekUsage
    {
        struct PromptTokensDetails
        {
            std::optional<std::uint64_t> cached_tokens;
        };

        struct CompletionTokensDetails
        {
            std::optional<std::uint64_t> reasoning_tokens;
        };

        std::uint64_t completion_tokens = 0;
        std::uint64_t prompt_tokens = 0;
        std::optional<PromptTokensDetails> prompt_tokens_details;
        std::uint64_t prompt_cache_hit_tokens = 0;
        std::uint64_t prompt_cache_miss_tokens = 0;
        std::uint64_t total_tokens = 0;
        std::optional<CompletionTokensDetails> completion_tokens_details;
    };
}
