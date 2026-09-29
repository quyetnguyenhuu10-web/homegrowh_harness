#include "context_usage.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace context_usage
{
    namespace
    {
        std::uint64_t utf8_characters(const std::string& text) noexcept
        {
            std::uint64_t count = 0;
            for (const unsigned char value : text)
            {
                if ((value & 0xC0u) != 0x80u)
                    ++count;
            }
            return count;
        }

        std::uint64_t serialized_estimate(const nlohmann::json& value)
        {
            return utf8_characters(value.dump()) / 4;
        }

        nlohmann::json model_visible_context(const nlohmann::json& body)
        {
            nlohmann::json context = nlohmann::json::object();

            const auto messages = body.find("messages");
            if (messages != body.end())
            {
                if (!messages->is_array())
                {
                    throw std::invalid_argument(
                        "session messages must be an array");
                }
                context["messages"] = *messages;
            }

            const auto tools = body.find("tools");
            if (tools != body.end())
            {
                if (!tools->is_array())
                {
                    throw std::invalid_argument(
                        "session tools must be an array");
                }
                context["tools"] = *tools;
            }

            return context;
        }

        std::uint64_t exact_add(
            std::uint64_t left,
            std::uint64_t right)
        {
            if (right > (std::numeric_limits<std::uint64_t>::max)() - left)
                throw std::overflow_error("provider: context usage overflow");
            return left + right;
        }

    }

    std::uint64_t estimate(const nlohmann::json& body)
    {
        if (body.is_array())
            return serialized_estimate(body);

        if (!body.is_object())
        {
            throw std::invalid_argument(
                "session::usage_es expects a message array or session object");
        }

        return serialized_estimate(model_visible_context(body));
    }

    std::uint64_t openai(
        std::uint64_t prompt_tokens,
        std::uint64_t completion_tokens)
    {
        std::uint64_t total = 0;
        total = exact_add(total, prompt_tokens);
        total = exact_add(total, completion_tokens);
        return total;
    }

    std::uint64_t deepseek(
        std::uint64_t prompt_cache_hit_tokens,
        std::uint64_t prompt_cache_miss_tokens,
        std::uint64_t completion_tokens)
    {
        std::uint64_t total = 0;
        total = exact_add(total, prompt_cache_hit_tokens);
        total = exact_add(total, prompt_cache_miss_tokens);
        total = exact_add(total, completion_tokens);
        return total;
    }

    std::uint64_t bonsai(
        std::uint64_t cache_n,
        std::uint64_t prompt_n,
        std::uint64_t predicted_n)
    {
        std::uint64_t total = 0;
        total = exact_add(total, cache_n);
        total = exact_add(total, prompt_n);
        total = exact_add(total, predicted_n);
        return total;
    }

}
