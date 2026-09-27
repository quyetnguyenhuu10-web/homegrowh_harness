#include "tool_call.h"

#include <event_port>
#include <sandbox>

#include <exception>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace sessions::detail
{
    namespace
    {
        event_port::References tool_references(const std::string& call_id)
        {
            event_port::References references;
            references.emplace_back("tool_call_id", std::string(call_id));
            return references;
        }

        std::string powershell_literal(std::string_view value)
        {
            std::string output;
            output.reserve(value.size() + 2);
            output.push_back('\'');
            for (const char ch : value)
            {
                output.push_back(ch);
                if (ch == '\'')
                    output.push_back('\'');
            }
            output.push_back('\'');
            return output;
        }

        std::string compose_body(
            std::string_view body,
            const nlohmann::json& information)
        {
            std::string raw;
            const std::string information_json = information.dump();
            raw.reserve(body.size() + information_json.size() + 32);
            raw += "$HH_INFORMATION_JSON = ";
            raw += powershell_literal(information_json);
            raw.push_back('\n');
            raw += body;
            return raw;
        }

        nlohmann::json error_item(
            const char* code,
            const std::string& message)
        {
            return {
                {"ok", false},
                {"error", {
                    {"code", code},
                    {"message", message}
                }}
            };
        }

        nlohmann::json parse_runtime_content(
            const nlohmann::json& runtime_message)
        {
            if (!runtime_message.is_object())
            {
                return error_item(
                    "invalid_tool_result",
                    "Tool runtime result message must be an object");
            }

            const auto content = runtime_message.find("content");
            if (content == runtime_message.end() || !content->is_string())
            {
                return error_item(
                    "invalid_tool_result",
                    "Tool runtime result message must contain string content");
            }

            nlohmann::json payload = nlohmann::json::parse(
                content->get_ref<const std::string&>(),
                nullptr,
                false);

            if (payload.is_discarded())
            {
                return {
                    {"ok", true},
                    {"result", content->get<std::string>()}
                };
            }

            if (payload.is_object())
            {
                const auto ok = payload.find("ok");
                if (ok != payload.end() && ok->is_boolean() && !ok->get<bool>())
                {
                    nlohmann::json item = {
                        {"ok", false},
                        {"result", payload}
                    };

                    const auto error = payload.find("error");
                    if (error != payload.end())
                        item["error"] = *error;

                    return item;
                }
            }

            return {
                {"ok", true},
                {"result", std::move(payload)}
            };
        }

        nlohmann::json result_message(
            const std::string& call_id,
            const nlohmann::json* definition,
            nlohmann::json result)
        {
            nlohmann::json results = nlohmann::json::array();
            results.push_back(std::move(result));

            const nlohmann::json envelope = {
                {"version", 1},
                {"tool", definition == nullptr
                    ? nlohmann::json(nullptr)
                    : *definition},
                {"call_id", call_id},
                {"results", std::move(results)}
            };

            return {
                {"role", "tool"},
                {"tool_call_id", call_id},
                {"content", envelope.dump()}
            };
        }

        std::string raw_name(const nlohmann::json& raw_tool_call)
        {
            if (!raw_tool_call.is_object())
                return {};

            const auto function = raw_tool_call.find("function");
            if (function == raw_tool_call.end() || !function->is_object())
                return {};

            const auto name = function->find("name");
            if (name == function->end() || !name->is_string())
                return {};

            return name->get<std::string>();
        }

        std::string raw_id(const nlohmann::json& raw_tool_call)
        {
            if (!raw_tool_call.is_object())
                return {};

            const auto id = raw_tool_call.find("id");
            if (
                id == raw_tool_call.end() ||
                !id->is_string() ||
                id->get_ref<const std::string&>().empty())
            {
                return {};
            }

            return id->get<std::string>();
        }

        bool normalize_arguments(
            const nlohmann::json& raw_tool_call,
            std::string& arguments,
            nlohmann::json& error)
        {
            if (!raw_tool_call.is_object())
            {
                error = error_item(
                    "invalid_tool_call",
                    "Tool call must be an object");
                arguments = "{}";
                return false;
            }

            const auto function = raw_tool_call.find("function");
            if (function == raw_tool_call.end() || !function->is_object())
            {
                error = error_item(
                    "invalid_tool_call",
                    "Tool call function must be an object");
                arguments = "{}";
                return false;
            }

            const auto source = function->find("arguments");
            if (source == function->end())
            {
                error = error_item(
                    "invalid_arguments",
                    "Tool call function.arguments is required");
                arguments = "{}";
                return false;
            }

            nlohmann::json parsed;
            if (source->is_string())
            {
                arguments = source->get<std::string>();
                parsed = nlohmann::json::parse(arguments, nullptr, false);
                if (parsed.is_discarded())
                {
                    error = error_item(
                        "invalid_arguments",
                        "Tool call function.arguments contains invalid JSON");
                    return false;
                }
            }
            else
            {
                parsed = *source;
                arguments = parsed.dump();
            }

            if (!parsed.is_object() && !parsed.is_array())
            {
                error = error_item(
                    "invalid_arguments",
                    "Tool call function.arguments must be an object or array");
                return false;
            }

            return true;
        }
    }

    ToolCallHandler::ToolCallHandler(
        const nlohmann::json& tool_definitions,
        const std::string& tool_body,
        const StreamCallback* stream)
        : tool_definitions_(tool_definitions),
          tool_body_(tool_body),
          stream_(stream)
    {
        if (!tool_definitions_.is_array())
            throw std::invalid_argument("tool_definitions must be an array");
    }

    const nlohmann::json* ToolCallHandler::definition(
        const std::string& name) const
    {
        for (const nlohmann::json& item : tool_definitions_)
        {
            if (!item.is_object())
                continue;

            const auto function = item.find("function");
            if (function == item.end() || !function->is_object())
                continue;

            const auto schema_name = function->find("name");
            if (
                schema_name != function->end() &&
                schema_name->is_string() &&
                schema_name->get_ref<const std::string&>() == name)
            {
                return &item;
            }
        }

        return nullptr;
    }

    std::string ToolCallHandler::next_call_id()
    {
        return "hh_tool_call_" + std::to_string(next_call_id_++);
    }

    void ToolCallHandler::emit(
        StreamType type,
        std::string_view call_id) const
    {
        if (stream_ != nullptr && *stream_)
            (*stream_)(type, call_id);
    }

    HandledToolCall ToolCallHandler::finish(
        nlohmann::json tool_call,
        nlohmann::json result_message_value) const
    {
        const std::string& call_id =
            tool_call.at("id").get_ref<const std::string&>();
        emit(StreamType::tool_result, call_id);
        return {
            std::move(tool_call),
            std::move(result_message_value)
        };
    }

    PreparedToolCall ToolCallHandler::prepare(
        const nlohmann::json& raw_tool_call)
    {
        const std::string name = raw_name(raw_tool_call);
        const nlohmann::json* schema = definition(name);
        const std::string raw_call_id = raw_id(raw_tool_call);
        const bool synthesized_id = raw_call_id.empty();
        const std::string call_id = synthesized_id
            ? "hh_tool_call_" + std::to_string(next_call_id_)
            : raw_call_id;

        std::string arguments;
        nlohmann::json preparation_error;
        const bool arguments_valid = normalize_arguments(
            raw_tool_call,
            arguments,
            preparation_error);

        const std::string canonical_name = schema == nullptr
            ? (name.empty() ? "__invalid_tool_call__" : name)
            : schema->at("function").at("name").get<std::string>();

        nlohmann::json canonical_call = {
            {"id", call_id},
            {"type", "function"},
            {"function", {
                {"name", canonical_name},
                {"arguments", arguments}
            }}
        };

        return PreparedToolCall{
            std::move(canonical_call),
            schema,
            std::move(preparation_error),
            arguments_valid,
            synthesized_id
        };
    }

    HandledToolCall ToolCallHandler::execute(PreparedToolCall&& prepared)
    {
        const std::string& call_id =
            prepared.tool_call.at("id").get_ref<const std::string&>();

        if (prepared.synthesized_id)
        {
            const std::string committed_id = next_call_id();
            if (committed_id != call_id)
                throw std::logic_error("prepared tool call id is stale");
        }

        emit(StreamType::tool_call, call_id);

        if (prepared.definition == nullptr)
        {
            const std::string missing_name =
                prepared.tool_call.at("function").at("name").get<std::string>();
            return finish(
                std::move(prepared.tool_call),
                result_message(
                    call_id,
                    nullptr,
                    error_item(
                        "tool_schema_not_found",
                        missing_name == "__invalid_tool_call__"
                            ? "Tool call has no function name"
                            : "Tool is not present in tool_definitions: " + missing_name)));
        }

        if (!prepared.arguments_valid)
        {
            return finish(
                std::move(prepared.tool_call),
                result_message(
                    call_id,
                    prepared.definition,
                    std::move(prepared.preparation_error)));
        }

        try
        {
            if (tool_body_.empty())
                throw std::runtime_error("tool body is empty");

            const nlohmann::json information = {
                {"tool_call", prepared.tool_call},
                {"read_files", read_files_}
            };

            event_port::Registration registration = event_port::port(
                event_port::Register{
                    "tools",
                    tool_references(call_id)
                });

            const std::string body = compose_body(tool_body_, information);
            std::exception_ptr sandbox_error;
            int sandbox_exit_code = -1;
            std::jthread sandbox_thread([&]() noexcept {
                try
                {
                    sandbox_exit_code = sandbox::run(body);
                }
                catch (...)
                {
                    sandbox_error = std::current_exception();
                }
                try
                {
                    event_port::port(event_port::Close{registration});
                }
                catch (...)
                {
                }
            });

            event_port::EventPtr profile_event;
            std::exception_ptr read_error;
            try
            {
                profile_event = event_port::port(
                    event_port::Read{registration});
            }
            catch (...)
            {
                read_error = std::current_exception();
            }

            sandbox_thread.join();
            if (sandbox_error != nullptr)
                std::rethrow_exception(sandbox_error);
            if (profile_event == nullptr)
            {
                if (read_error != nullptr && sandbox_exit_code == 0)
                    std::rethrow_exception(read_error);
                throw std::runtime_error(
                    "tool profile body completed without a result event; exit_code="
                    + std::to_string(sandbox_exit_code));
            }
            if (profile_event->type == "failed")
            {
                throw std::runtime_error(profile_event->data.value(
                    "raw",
                    std::string("tool profile failed")));
            }
            if (profile_event->type != "result")
            {
                throw std::runtime_error(
                    "tool profile emitted unexpected event type: "
                    + profile_event->type);
            }
            if (sandbox_exit_code != 0)
            {
                throw std::runtime_error(
                    "tool profile body exited with code "
                    + std::to_string(sandbox_exit_code));
            }

            const std::string profile_response =
                profile_event->data.at("raw").get<std::string>();

            const nlohmann::json runtime_result = nlohmann::json::parse(
                profile_response,
                nullptr,
                false);
            if (runtime_result.is_discarded())
                throw std::runtime_error("tool profile returned invalid JSON");
            if (!runtime_result.is_object())
                throw std::runtime_error("tool profile response must be an object");

            const auto result_message_value = runtime_result.find("result_message");
            const auto read_files_value = runtime_result.find("read_files");
            if (result_message_value == runtime_result.end()
                || !result_message_value->is_object()
                || read_files_value == runtime_result.end()
                || !read_files_value->is_array())
            {
                throw std::runtime_error("tool profile response is incomplete");
            }

            std::vector<std::string> next_read_files;
            next_read_files.reserve(read_files_value->size());
            for (const nlohmann::json& item : *read_files_value)
            {
                if (!item.is_string())
                    throw std::runtime_error("tool profile read_files item must be a string");
                next_read_files.push_back(item.get<std::string>());
            }
            read_files_ = std::move(next_read_files);

            return finish(
                std::move(prepared.tool_call),
                result_message(
                    call_id,
                    prepared.definition,
                    parse_runtime_content(*result_message_value)));
        }
        catch (const std::invalid_argument& error)
        {
            return finish(
                std::move(prepared.tool_call),
                result_message(
                    call_id,
                    prepared.definition,
                    error_item(
                        "invalid_tool_call_schema",
                        error.what())));
        }
        catch (const std::exception& error)
        {
            return finish(
                std::move(prepared.tool_call),
                result_message(
                    call_id,
                    prepared.definition,
                    error_item(
                        "tool_execution_error",
                        error.what())));
        }
    }

    HandledToolCall ToolCallHandler::handle(
        const nlohmann::json& raw_tool_call)
    {
        return execute(prepare(raw_tool_call));
    }
}
