#include "tool_stream_parser.h"

#include <cstdint>
#include <string>
#include <utility>

namespace sessions::detail
{
    namespace
    {
        void merge_fragment(
            nlohmann::json& target,
            std::string_view key,
            const nlohmann::json& value) noexcept
        {
            if (value.is_null())
                return;

            try
            {
                const std::string field(key);
                auto current = target.find(field);
                if (current == target.end() || current->is_null())
                {
                    target[field] = value;
                    return;
                }

                if (current->is_string() && value.is_string())
                {
                    current->get_ref<std::string&>() +=
                        value.get_ref<const std::string&>();
                }
            }
            catch (...)
            {
            }
        }
    }

    void ToolStreamParser::append(std::string_view event) noexcept
    {
        append_impl(event);
    }

    void ToolStreamParser::append_impl(std::string_view event) noexcept
    {
        try
        {
            if (event.empty() || event == "[DONE]")
                return;

            const nlohmann::json payload = nlohmann::json::parse(
                event,
                nullptr,
                false);
            if (payload.is_discarded() || !payload.is_object())
                return;

            const auto choices = payload.find("choices");
            if (choices == payload.end() || !choices->is_array())
                return;

            for (const nlohmann::json& choice : *choices)
            {
                if (!choice.is_object())
                    continue;

                const auto delta = choice.find("delta");
                if (delta == choice.end() || !delta->is_object())
                    continue;

                const auto calls = delta->find("tool_calls");
                if (calls == delta->end() || calls->is_null())
                    continue;
                if (!calls->is_array())
                {
                    unindexed_calls_.push_back(*calls);
                    continue;
                }

                for (const nlohmann::json& call : *calls)
                {
                    if (!call.is_object())
                    {
                        unindexed_calls_.push_back(call);
                        continue;
                    }

                    const auto index = call.find("index");
                    if (index == call.end() || !index->is_number_integer())
                    {
                        unindexed_calls_.push_back(call);
                        continue;
                    }

                    const std::int64_t tool_index = index->get<std::int64_t>();
                    if (tool_index < 0)
                    {
                        unindexed_calls_.push_back(call);
                        continue;
                    }

                    nlohmann::json& target =
                        tool_calls_[static_cast<std::size_t>(tool_index)];
                    if (!target.is_object())
                        target = nlohmann::json::object();

                    const auto id = call.find("id");
                    if (id != call.end() && !id->is_null())
                        target["id"] = *id;

                    const auto type = call.find("type");
                    if (type != call.end() && !type->is_null())
                        target["type"] = *type;

                    const auto function = call.find("function");
                    if (function == call.end() || function->is_null())
                        continue;
                    if (!function->is_object())
                    {
                        target["function"] = *function;
                        continue;
                    }

                    const auto existing_function = target.find("function");
                    if (
                        existing_function != target.end() &&
                        !existing_function->is_null() &&
                        !existing_function->is_object())
                    {
                        continue;
                    }

                    nlohmann::json& target_function = target["function"];
                    if (target_function.is_null())
                        target_function = nlohmann::json::object();

                    const auto name = function->find("name");
                    if (name != function->end())
                        merge_fragment(target_function, "name", *name);

                    const auto arguments = function->find("arguments");
                    if (arguments != function->end())
                        merge_fragment(target_function, "arguments", *arguments);
                }
            }
        }
        catch (...)
        {
        }
    }

    nlohmann::json ToolStreamParser::finish()
    {
        nlohmann::json result = nlohmann::json::array();
        for (auto& [index, call] : tool_calls_)
        {
            (void)index;
            result.push_back(std::move(call));
        }
        for (nlohmann::json& call : unindexed_calls_)
            result.push_back(std::move(call));

        tool_calls_.clear();
        unindexed_calls_.clear();
        return result;
    }
}
