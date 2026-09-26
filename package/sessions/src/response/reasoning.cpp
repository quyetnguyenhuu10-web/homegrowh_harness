#include "reasoning.h"

#include <nlohmann/json.hpp>

#include <stdexcept>

namespace sessions::detail
{
    namespace
    {
        std::optional<std::string_view> reasoning_content(
            const nlohmann::json& delta)
        {
            const auto reasoning = delta.find("reasoning_content");
            if (reasoning == delta.end() || reasoning->is_null())
                return std::nullopt;
            if (!reasoning->is_string())
            {
                throw std::runtime_error(
                    "provider delta reasoning_content must be a string");
            }

            const std::string& text = reasoning->get_ref<const std::string&>();
            if (text.empty())
                return std::nullopt;
            return std::string_view(text);
        }
    }

    std::optional<std::string_view> reasoning_delta(
        provider::Provider provider,
        const nlohmann::json& delta)
    {
        switch (provider)
        {
            case provider::Provider::openai:
                // OpenAI Chat Completions does not expose hidden reasoning text.
                return std::nullopt;

            case provider::Provider::deepseek:
                return reasoning_content(delta);

            case provider::Provider::bonsai:
                return reasoning_content(delta);
        }

        throw std::runtime_error("unsupported provider reasoning parser");
    }
}
