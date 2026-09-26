#include "response.h"
#include "reasoning.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

namespace sessions::detail
{
    namespace
    {
        void append_string(
            const nlohmann::json& object,
            const char* key,
            std::string& output)
        {
            const auto value = object.find(key);
            if (value == object.end() || value->is_null())
                return;
            if (!value->is_string())
            {
                throw std::runtime_error(
                    std::string("provider delta field must be a string: ") + key);
            }
            output += value->get_ref<const std::string&>();
        }
    }

    ResponseBuilder::ResponseBuilder(
        provider::Provider provider,
        const StreamCallback* stream) noexcept
        : provider_(provider),
          stream_(stream)
    {
    }

    void ResponseBuilder::append(std::string_view event)
    {
        if (event.empty() || event == "[DONE]")
            return;

        const nlohmann::json payload = nlohmann::json::parse(
            event,
            nullptr,
            false);
        if (payload.is_discarded() || !payload.is_object())
            throw std::runtime_error("provider event is not a valid JSON object");

        const auto choices = payload.find("choices");
        if (choices == payload.end() || choices->is_null())
            return;
        if (!choices->is_array())
            throw std::runtime_error("provider event choices must be an array");

        for (const nlohmann::json& choice : *choices)
        {
            if (!choice.is_object())
                throw std::runtime_error("provider choice must be an object");

            const auto choice_index = choice.find("index");
            if (
                choice_index != choice.end() &&
                (!choice_index->is_number_integer() ||
                 choice_index->get<std::int64_t>() != 0))
            {
                throw std::runtime_error(
                    "only choices[0] can be projected into linear session history");
            }

            const auto delta = choice.find("delta");
            if (delta == choice.end() || delta->is_null())
                continue;
            if (!delta->is_object())
                throw std::runtime_error("provider choice delta must be an object");

            started_ = true;

            const auto role = delta->find("role");
            if (role != delta->end() && !role->is_null())
            {
                if (!role->is_string())
                    throw std::runtime_error("provider delta role must be a string");
                role_ = role->get<std::string>();
            }

            const auto content = delta->find("content");
            if (content != delta->end() && !content->is_null())
            {
                if (!content->is_string())
                    throw std::runtime_error("provider delta content must be a string");
                has_content_ = true;
                const std::string& text = content->get_ref<const std::string&>();
                content_ += text;
                if (stream_ != nullptr && *stream_ && !text.empty())
                    (*stream_)(StreamType::content, text);
            }

            if (const auto reasoning = reasoning_delta(provider_, *delta))
            {
                has_reasoning_ = true;
                reasoning_content_ += *reasoning;
                if (stream_ != nullptr && *stream_)
                    (*stream_)(StreamType::reasoning, *reasoning);
            }

            const auto calls = delta->find("tool_calls");
            if (calls == delta->end() || calls->is_null())
                continue;
            if (!calls->is_array())
                throw std::runtime_error("provider delta tool_calls must be an array");

            for (const nlohmann::json& call : *calls)
            {
                if (!call.is_object())
                    throw std::runtime_error("provider tool call delta must be an object");

                const auto index = call.find("index");
                if (index == call.end() || !index->is_number_integer())
                    throw std::runtime_error("provider tool call index is required");
                const std::int64_t tool_index = index->get<std::int64_t>();
                if (tool_index < 0)
                    throw std::runtime_error("provider tool call index must be non-negative");

                ToolCall& target =
                    tool_calls_[static_cast<std::size_t>(tool_index)];

                const auto id = call.find("id");
                if (id != call.end() && !id->is_null())
                {
                    if (!id->is_string())
                        throw std::runtime_error("provider tool call id must be a string");
                    target.id = id->get<std::string>();
                }

                const auto type = call.find("type");
                if (type != call.end() && !type->is_null())
                {
                    if (!type->is_string())
                        throw std::runtime_error("provider tool call type must be a string");
                    target.type = type->get<std::string>();
                }

                const auto function = call.find("function");
                if (function == call.end() || function->is_null())
                    continue;
                if (!function->is_object())
                    throw std::runtime_error("provider tool call function must be an object");

                append_string(*function, "name", target.name);

                const auto arguments = function->find("arguments");
                if (arguments == function->end() || arguments->is_null())
                    continue;
                if (arguments->is_string())
                {
                    target.arguments += arguments->get_ref<const std::string&>();
                }
                else if (target.arguments.empty())
                {
                    target.arguments = arguments->dump();
                }
                else
                {
                    throw std::runtime_error(
                        "provider tool call arguments changed representation mid-stream");
                }
            }
        }
    }

    nlohmann::json ResponseBuilder::finish()
    {
        if (!started_)
            throw std::runtime_error("provider response contained no assistant delta");

        nlohmann::json result = {
            {"role", role_},
            {"content", has_content_
                ? nlohmann::json(std::move(content_))
                : nlohmann::json(nullptr)}
        };

        if (has_reasoning_)
            result["reasoning_content"] = std::move(reasoning_content_);

        if (!tool_calls_.empty())
        {
            nlohmann::json calls = nlohmann::json::array();
            for (auto& [index, call] : tool_calls_)
            {
                (void)index;
                calls.push_back({
                    {"id", std::move(call.id)},
                    {"type", call.type.empty() ? "function" : std::move(call.type)},
                    {"function", {
                        {"name", std::move(call.name)},
                        {"arguments", std::move(call.arguments)}
                    }}
                });
            }
            result["tool_calls"] = std::move(calls);
        }

        return result;
    }
}
