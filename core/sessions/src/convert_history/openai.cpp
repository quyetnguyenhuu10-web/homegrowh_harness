#include <sessions>

#include <events>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace sessions::detail
{
    nlohmann::json convert_openai_history(
        const std::vector<events::Event>& rows);

    nlohmann::json convert_deepseek_history(
        const std::vector<events::Event>& rows);

    nlohmann::json convert_bonsai_history(
        const std::vector<events::Event>& rows);

    nlohmann::json convert_history_rows(
        const std::vector<events::Event>& rows)
    {
        nlohmann::json history = nlohmann::json::array();
        std::size_t begin = 0;

        while (begin < rows.size())
        {
            const std::string& provider = rows[begin].provider;
            std::size_t end = begin + 1;
            while (
                end < rows.size() &&
                rows[end].provider == provider)
            {
                ++end;
            }

            const std::vector<events::Event> segment(
                rows.begin() + static_cast<std::ptrdiff_t>(begin),
                rows.begin() + static_cast<std::ptrdiff_t>(end));

            nlohmann::json converted;
            if (provider == "openai")
                converted = convert_openai_history(segment);
            else if (provider == "deepseek")
                converted = convert_deepseek_history(segment);
            else if (provider == "bonsai")
                converted = convert_bonsai_history(segment);
            else
            {
                throw std::runtime_error(
                    "convert_history: unsupported provider: " + provider);
            }

            for (nlohmann::json& message : converted)
                history.push_back(std::move(message));

            begin = end;
        }

        return history;
    }

    namespace
    {
        struct ToolCall
        {
            std::string id;
            std::string type;
            std::string name;
            std::string arguments;
        };

        struct Message
        {
            bool started = false;
            bool has_content = false;
            std::string response_id;
            std::string role = "assistant";
            std::string content;
            std::map<std::size_t, ToolCall> tool_calls;
        };

        [[noreturn]] void invalid_event(
            const events::Event& row,
            const std::string& message)
        {
            throw std::runtime_error(
                "convert_history openai row " +
                std::to_string(row.row_position) +
                ": " +
                message);
        }

        void append_string(
            const nlohmann::json& object,
            const char* key,
            std::string& output,
            const events::Event& row)
        {
            const auto value = object.find(key);
            if (value == object.end() || value->is_null())
                return;
            if (!value->is_string())
                invalid_event(row, std::string(key) + " must be a string");
            output += value->get_ref<const std::string&>();
        }

        void apply_tool_calls(
            const nlohmann::json& delta,
            Message& message,
            const events::Event& row)
        {
            const auto calls = delta.find("tool_calls");
            if (calls == delta.end() || calls->is_null())
                return;
            if (!calls->is_array())
                invalid_event(row, "delta.tool_calls must be an array");

            for (const nlohmann::json& call : *calls)
            {
                if (!call.is_object())
                    invalid_event(row, "tool call delta must be an object");

                const auto index = call.find("index");
                if (index == call.end() || !index->is_number_integer())
                    invalid_event(row, "tool call delta index is required");
                const std::int64_t tool_index = index->get<std::int64_t>();
                if (tool_index < 0)
                    invalid_event(row, "tool call delta index must be non-negative");

                ToolCall& target =
                    message.tool_calls[static_cast<std::size_t>(tool_index)];

                const auto id = call.find("id");
                if (id != call.end() && !id->is_null())
                {
                    if (!id->is_string())
                        invalid_event(row, "tool call id must be a string");
                    target.id = id->get<std::string>();
                }

                const auto type = call.find("type");
                if (type != call.end() && !type->is_null())
                {
                    if (!type->is_string())
                        invalid_event(row, "tool call type must be a string");
                    target.type = type->get<std::string>();
                }

                const auto function = call.find("function");
                if (function == call.end() || function->is_null())
                    continue;
                if (!function->is_object())
                    invalid_event(row, "tool call function must be an object");

                append_string(*function, "name", target.name, row);
                append_string(
                    *function,
                    "arguments",
                    target.arguments,
                    row);
            }
        }

        nlohmann::json finish_message(
            Message& message,
            const events::Event& row)
        {
            nlohmann::json output = {
                {"role", message.role}
            };

            if (message.has_content)
                output["content"] = std::move(message.content);
            else
                output["content"] = nullptr;

            if (!message.tool_calls.empty())
            {
                nlohmann::json calls = nlohmann::json::array();
                for (auto& [index, call] : message.tool_calls)
                {
                    (void)index;
                    if (call.id.empty())
                        invalid_event(row, "completed tool call has no id");
                    if (call.name.empty())
                        invalid_event(row, "completed tool call has no function name");

                    calls.push_back({
                        {"id", std::move(call.id)},
                        {"type", call.type.empty() ? "function" : std::move(call.type)},
                        {"function", {
                            {"name", std::move(call.name)},
                            {"arguments", std::move(call.arguments)}
                        }}
                    });
                }
                output["tool_calls"] = std::move(calls);
            }

            message = Message{};
            return output;
        }
    }

    nlohmann::json convert_openai_history(
        const std::vector<events::Event>& rows)
    {
        nlohmann::json history = nlohmann::json::array();
        Message current;
        const events::Event* last_row = nullptr;

        for (const events::Event& row : rows)
        {
            last_row = &row;
            if (row.events == "[DONE]")
                continue;

            const nlohmann::json event = nlohmann::json::parse(
                row.events,
                nullptr,
                false);
            if (event.is_discarded() || !event.is_object())
                invalid_event(row, "event is not valid JSON object");

            const auto choices = event.find("choices");
            if (choices == event.end() || choices->is_null())
                continue;
            if (!choices->is_array())
                invalid_event(row, "choices must be an array");

            for (const nlohmann::json& choice : *choices)
            {
                if (!choice.is_object())
                    invalid_event(row, "choice must be an object");

                const auto choice_index = choice.find("index");
                if (
                    choice_index != choice.end() &&
                    (!choice_index->is_number_integer() ||
                     choice_index->get<std::int64_t>() != 0))
                {
                    invalid_event(
                        row,
                        "only choices[0] can be projected into linear history");
                }

                const auto delta = choice.find("delta");
                if (delta == choice.end() || !delta->is_object())
                    continue;

                const std::string response_id =
                    event.value("id", std::string{});
                if (
                    current.started &&
                    !response_id.empty() &&
                    !current.response_id.empty() &&
                    response_id != current.response_id)
                {
                    history.push_back(finish_message(current, row));
                }

                if (!current.started)
                {
                    current.started = true;
                    current.response_id = response_id;
                }

                const auto role = delta->find("role");
                if (role != delta->end() && !role->is_null())
                {
                    if (!role->is_string())
                        invalid_event(row, "delta.role must be a string");
                    current.role = role->get<std::string>();
                }

                const auto content = delta->find("content");
                if (content != delta->end() && !content->is_null())
                {
                    if (!content->is_string())
                        invalid_event(row, "delta.content must be a string");
                    current.has_content = true;
                    current.content += content->get_ref<const std::string&>();
                }

                apply_tool_calls(*delta, current, row);

                const auto finish_reason = choice.find("finish_reason");
                if (
                    finish_reason != choice.end() &&
                    !finish_reason->is_null())
                {
                    history.push_back(finish_message(current, row));
                }
            }
        }

        if (current.started)
        {
            if (last_row == nullptr)
                throw std::logic_error("convert_history openai: missing last row");
            history.push_back(finish_message(current, *last_row));
        }

        return history;
    }
}

namespace sessions
{
    namespace
    {
        void validate_database_path(
            const std::filesystem::path& database_path)
        {
            if (database_path.empty())
            {
                throw std::invalid_argument(
                    "convert_history: database_path is empty");
            }

            if (!std::filesystem::is_regular_file(database_path))
            {
                throw std::filesystem::filesystem_error(
                    "convert_history: database does not exist",
                    database_path,
                    std::make_error_code(std::errc::no_such_file_or_directory));
            }
        }
    }

    nlohmann::json convert_history(
        const std::filesystem::path& database_path,
        const std::string& session_id)
    {
        validate_database_path(database_path);
        if (session_id.empty())
            throw std::invalid_argument("convert_history: session_id is empty");

        events::Store store(database_path);
        const std::vector<events::Event> rows =
            events::query(store, session_id);

        return detail::convert_history_rows(rows);
    }

    nlohmann::json convert_all_history(
        const std::filesystem::path& database_path)
    {
        validate_database_path(database_path);

        events::Store store(database_path);
        const std::vector<events::Event> rows = events::query(store);
        nlohmann::json result = nlohmann::json::array();
        std::size_t begin = 0;

        while (begin < rows.size())
        {
            const std::string& session_id = rows[begin].session_id;
            std::size_t end = begin + 1;
            while (
                end < rows.size() &&
                rows[end].session_id == session_id)
            {
                ++end;
            }

            const std::vector<events::Event> session_rows(
                rows.begin() + static_cast<std::ptrdiff_t>(begin),
                rows.begin() + static_cast<std::ptrdiff_t>(end));

            result.push_back({
                {"session_id", session_id},
                {"history", detail::convert_history_rows(session_rows)}
            });

            begin = end;
        }

        return result;
    }
}
