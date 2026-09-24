#pragma once

#include "catalog.h"
#include "requests.h"

#include <cstdint>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace provider
{
    namespace
    {
        std::optional<std::uint64_t> optional_uint64(
            const nlohmann::json& object,
            const char* field)
        {
            const auto found = object.find(field);

            if (found == object.end() || found->is_null())
            {
                return std::nullopt;
            }

            return found->get<std::uint64_t>();
        }

        OpenAIUsage parse_openai_usage(const nlohmann::json& usage)
        {
            OpenAIUsage result;
            result.completion_tokens = usage.at("completion_tokens").get<std::uint64_t>();
            result.prompt_tokens = usage.at("prompt_tokens").get<std::uint64_t>();
            result.total_tokens = usage.at("total_tokens").get<std::uint64_t>();

            if (
                const auto found = usage.find("prompt_tokens_details");
                found != usage.end() && !found->is_null())
            {
                OpenAIUsage::PromptTokensDetails details;
                details.audio_tokens = optional_uint64(*found, "audio_tokens");
                details.cache_write_tokens = optional_uint64(
                    *found,
                    "cache_write_tokens");
                details.cached_tokens = optional_uint64(*found, "cached_tokens");
                details.image_tokens = optional_uint64(*found, "image_tokens");
                details.text_tokens = optional_uint64(*found, "text_tokens");
                result.prompt_tokens_details = details;
            }

            if (
                const auto found = usage.find("completion_tokens_details");
                found != usage.end() && !found->is_null())
            {
                OpenAIUsage::CompletionTokensDetails details;
                details.accepted_prediction_tokens = optional_uint64(
                    *found,
                    "accepted_prediction_tokens");
                details.audio_tokens = optional_uint64(*found, "audio_tokens");
                details.reasoning_tokens = optional_uint64(*found, "reasoning_tokens");
                details.rejected_prediction_tokens = optional_uint64(
                    *found,
                    "rejected_prediction_tokens");
                details.text_tokens = optional_uint64(*found, "text_tokens");
                result.completion_tokens_details = details;
            }

            return result;
        }

        DeepSeekUsage parse_deepseek_usage(const nlohmann::json& usage)
        {
            DeepSeekUsage result;
            result.completion_tokens = usage.at("completion_tokens").get<std::uint64_t>();
            result.prompt_tokens = usage.at("prompt_tokens").get<std::uint64_t>();
            result.prompt_cache_hit_tokens =
                usage.at("prompt_cache_hit_tokens").get<std::uint64_t>();
            result.prompt_cache_miss_tokens =
                usage.at("prompt_cache_miss_tokens").get<std::uint64_t>();
            result.total_tokens = usage.at("total_tokens").get<std::uint64_t>();

            if (
                const auto found = usage.find("prompt_tokens_details");
                found != usage.end() && !found->is_null())
            {
                DeepSeekUsage::PromptTokensDetails details;
                details.cached_tokens = optional_uint64(*found, "cached_tokens");
                result.prompt_tokens_details = details;
            }

            if (
                const auto found = usage.find("completion_tokens_details");
                found != usage.end() && !found->is_null())
            {
                DeepSeekUsage::CompletionTokensDetails details;
                details.reasoning_tokens = optional_uint64(*found, "reasoning_tokens");
                result.completion_tokens_details = details;
            }

            return result;
        }

        BonsaiUsage parse_bonsai_usage(const nlohmann::json& usage)
        {
            BonsaiUsage result;
            result.cache_n = usage.at("cache_n").get<std::uint64_t>();
            result.prompt_n = usage.at("prompt_n").get<std::uint64_t>();
            result.prompt_ms = usage.at("prompt_ms").get<double>();
            result.prompt_per_token_ms =
                usage.at("prompt_per_token_ms").get<double>();
            result.prompt_per_second =
                usage.at("prompt_per_second").get<double>();
            result.predicted_n = usage.at("predicted_n").get<std::uint64_t>();
            result.predicted_ms = usage.at("predicted_ms").get<double>();
            result.predicted_per_token_ms =
                usage.at("predicted_per_token_ms").get<double>();
            result.predicted_per_second =
                usage.at("predicted_per_second").get<double>();
            return result;
        }

        RequestUsage parse_usage(
            UsageProvider provider,
            const nlohmann::json& usage)
        {
            switch (provider)
            {
                case UsageProvider::openai:
                    return parse_openai_usage(usage);

                case UsageProvider::deepseek:
                    return parse_deepseek_usage(usage);

                case UsageProvider::bonsai:
                    return parse_bonsai_usage(usage);
            }

            throw std::logic_error("unsupported usage provider");
        }
    }
}
