#pragma once

#include <cstdint>
#include <optional>

namespace provider
{
    struct OpenAIUsage
    {
        struct PromptTokensDetails
        {
            std::optional<std::uint64_t> audio_tokens;
            std::optional<std::uint64_t> cache_write_tokens;
            std::optional<std::uint64_t> cached_tokens;
            std::optional<std::uint64_t> image_tokens;
            std::optional<std::uint64_t> text_tokens;
        };

        struct CompletionTokensDetails
        {
            std::optional<std::uint64_t> accepted_prediction_tokens;
            std::optional<std::uint64_t> audio_tokens;
            std::optional<std::uint64_t> reasoning_tokens;
            std::optional<std::uint64_t> rejected_prediction_tokens;
            std::optional<std::uint64_t> text_tokens;
        };

        std::uint64_t completion_tokens = 0;
        std::uint64_t prompt_tokens = 0;
        std::uint64_t total_tokens = 0;
        std::optional<PromptTokensDetails> prompt_tokens_details;
        std::optional<CompletionTokensDetails> completion_tokens_details;
    };
}
