#include "wire_json.h"

#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>
#include <variant>

namespace sessions_runtime
{
    namespace
    {
        template <typename T>
        void put_optional(
            nlohmann::json& target,
            const char* key,
            const std::optional<T>& value)
        {
            if (value.has_value())
                target[key] = *value;
        }
    }

    const char* provider_name(provider::Provider value) noexcept
    {
        switch (value)
        {
            case provider::Provider::openai:
                return "openai";
            case provider::Provider::deepseek:
                return "deepseek";
            case provider::Provider::bonsai:
                return "bonsai";
        }
        return "unknown";
    }

    const char* session_state_name(
        sessions::SessionState state) noexcept
    {
        switch (state)
        {
            case sessions::SessionState::request:
                return "request";
            case sessions::SessionState::response:
                return "response";
            case sessions::SessionState::tool:
                return "tool";
            case sessions::SessionState::finished:
                return "finished";
            case sessions::SessionState::closed:
                return "closed";
        }
        return "unknown";
    }

    nlohmann::json usage_json(
        const provider::RequestUsage& usage)
    {
        if (const auto* value =
                std::get_if<provider::OpenAIUsage>(&usage))
        {
            nlohmann::json result = {
                {"provider", "openai"},
                {"prompt_tokens", value->prompt_tokens},
                {"completion_tokens", value->completion_tokens},
                {"total_tokens", value->total_tokens}
            };

            if (value->prompt_tokens_details.has_value())
            {
                nlohmann::json details = nlohmann::json::object();
                put_optional(
                    details,
                    "audio_tokens",
                    value->prompt_tokens_details->audio_tokens);
                put_optional(
                    details,
                    "cache_write_tokens",
                    value->prompt_tokens_details->cache_write_tokens);
                put_optional(
                    details,
                    "cached_tokens",
                    value->prompt_tokens_details->cached_tokens);
                put_optional(
                    details,
                    "image_tokens",
                    value->prompt_tokens_details->image_tokens);
                put_optional(
                    details,
                    "text_tokens",
                    value->prompt_tokens_details->text_tokens);
                result["prompt_tokens_details"] = std::move(details);
            }

            if (value->completion_tokens_details.has_value())
            {
                nlohmann::json details = nlohmann::json::object();
                put_optional(
                    details,
                    "accepted_prediction_tokens",
                    value->completion_tokens_details
                        ->accepted_prediction_tokens);
                put_optional(
                    details,
                    "audio_tokens",
                    value->completion_tokens_details->audio_tokens);
                put_optional(
                    details,
                    "reasoning_tokens",
                    value->completion_tokens_details->reasoning_tokens);
                put_optional(
                    details,
                    "rejected_prediction_tokens",
                    value->completion_tokens_details
                        ->rejected_prediction_tokens);
                put_optional(
                    details,
                    "text_tokens",
                    value->completion_tokens_details->text_tokens);
                result["completion_tokens_details"] = std::move(details);
            }
            return result;
        }

        if (const auto* value =
                std::get_if<provider::DeepSeekUsage>(&usage))
        {
            nlohmann::json result = {
                {"provider", "deepseek"},
                {"prompt_tokens", value->prompt_tokens},
                {"completion_tokens", value->completion_tokens},
                {"total_tokens", value->total_tokens},
                {"prompt_cache_hit_tokens",
                    value->prompt_cache_hit_tokens},
                {"prompt_cache_miss_tokens",
                    value->prompt_cache_miss_tokens}
            };

            if (value->prompt_tokens_details.has_value())
            {
                nlohmann::json details = nlohmann::json::object();
                put_optional(
                    details,
                    "cached_tokens",
                    value->prompt_tokens_details->cached_tokens);
                result["prompt_tokens_details"] = std::move(details);
            }

            if (value->completion_tokens_details.has_value())
            {
                nlohmann::json details = nlohmann::json::object();
                put_optional(
                    details,
                    "reasoning_tokens",
                    value->completion_tokens_details->reasoning_tokens);
                result["completion_tokens_details"] = std::move(details);
            }
            return result;
        }

        if (const auto* value =
                std::get_if<provider::BonsaiUsage>(&usage))
        {
            return nlohmann::json{
                {"provider", "bonsai"},
                {"cache_n", value->cache_n},
                {"prompt_n", value->prompt_n},
                {"prompt_ms", value->prompt_ms},
                {"prompt_per_token_ms", value->prompt_per_token_ms},
                {"prompt_per_second", value->prompt_per_second},
                {"predicted_n", value->predicted_n},
                {"predicted_ms", value->predicted_ms},
                {"predicted_per_token_ms",
                    value->predicted_per_token_ms},
                {"predicted_per_second",
                    value->predicted_per_second}
            };
        }

        return nlohmann::json{{"state", "unavailable"}};
    }

    nlohmann::json error_json(std::exception_ptr error)
    {
        if (error == nullptr)
            return nlohmann::json{{"message", nullptr}};

        try
        {
            std::rethrow_exception(error);
        }
        catch (const std::filesystem::filesystem_error& exception)
        {
            nlohmann::json result = {
                {"kind", "filesystem_error"},
                {"message", exception.what()},
                {"code", exception.code().value()},
                {"category", exception.code().category().name()}
            };
            if (!exception.path1().empty())
                result["path1"] = exception.path1().string();
            if (!exception.path2().empty())
                result["path2"] = exception.path2().string();
            return result;
        }
        catch (const std::system_error& exception)
        {
            return nlohmann::json{
                {"kind", "system_error"},
                {"message", exception.what()},
                {"code", exception.code().value()},
                {"category", exception.code().category().name()}
            };
        }
        catch (const std::exception& exception)
        {
            return nlohmann::json{
                {"kind", "exception"},
                {"message", exception.what()}
            };
        }
        catch (...)
        {
            return nlohmann::json{
                {"kind", "unknown"},
                {"message", nullptr}
            };
        }
    }
}
