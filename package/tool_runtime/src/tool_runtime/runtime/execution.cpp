#include "runtime.h"

#include "../process/process.h"

#include <chrono>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace tool_runtime::detail
{
    namespace
    {
        struct prepared_call
        {
            nlohmann::json canonical;
            const nlohmann::json* definition = nullptr;
            std::optional<nlohmann::json> error;
        };

        std::string synthesized_call_id()
        {
            const auto stamp =
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count();
#if defined(_WIN32)
            return "hh_tool_call_" +
                std::to_string(static_cast<unsigned long>(GetCurrentProcessId())) +
                "_" + std::to_string(stamp);
#elif defined(__linux__)
            return "hh_tool_call_" +
                std::to_string(static_cast<long long>(getpid())) +
                "_" + std::to_string(stamp);
#endif
        }

        std::filesystem::path current_executable()
        {
#if defined(_WIN32)
            std::wstring buffer(32768, L'\0');
            const DWORD written = GetModuleFileNameW(
                nullptr,
                buffer.data(),
                static_cast<DWORD>(buffer.size()));
            if (written == 0 || written >= buffer.size())
            {
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "GetModuleFileNameW(tool_runtime definitions)");
            }
            buffer.resize(written);
            return std::filesystem::path(std::move(buffer));
#elif defined(__linux__)
            std::string buffer(4096, '\0');
            const ssize_t written = readlink(
                "/proc/self/exe",
                buffer.data(),
                buffer.size());
            if (written < 0)
            {
                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "readlink(/proc/self/exe)");
            }
            buffer.resize(static_cast<std::size_t>(written));
            return std::filesystem::path(std::move(buffer));
#endif
        }

        std::filesystem::path tool_definitions_path()
        {
            if (const char* override_path =
                    std::getenv("HOMEGROWPH_TOOL_DEFINITIONS");
                override_path != nullptr && *override_path != '\0')
            {
                return std::filesystem::path(override_path);
            }

            return current_executable().parent_path()
                / "tool_runtime_tools"
                / "tool_definitions.json";
        }

        const nlohmann::json& tool_definitions()
        {
            static const nlohmann::json definitions = []
            {
                const std::filesystem::path path = tool_definitions_path();
                std::ifstream input(path, std::ios::binary);
                if (!input)
                {
                    throw std::runtime_error(
                        "tool_runtime: cannot open tool definitions: " +
                        path.string());
                }

                nlohmann::json parsed;
                input >> parsed;
                if (!parsed.is_array())
                {
                    throw std::runtime_error(
                        "tool_runtime: tool definitions must be an array");
                }
                return parsed;
            }();
            return definitions;
        }

        const nlohmann::json* find_definition(std::string_view name)
        {
            for (const nlohmann::json& item : tool_definitions())
            {
                if (!item.is_object())
                    continue;

                const auto function = item.find("function");
                if (function == item.end() || !function->is_object())
                    continue;

                const auto schema_name = function->find("name");
                if (schema_name != function->end()
                    && schema_name->is_string()
                    && schema_name->get_ref<const std::string&>() == name)
                {
                    return &item;
                }
            }
            return nullptr;
        }

        nlohmann::json error_item(
            std::string code,
            std::string message,
            nlohmann::json details = nlohmann::json::object())
        {
            nlohmann::json error = {
                {"code", std::move(code)},
                {"message", std::move(message)}
            };
            for (auto& [key, value] : details.items())
                error[key] = std::move(value);

            return {
                {"ok", false},
                {"error", std::move(error)}
            };
        }

        bool matches_type(
            std::string_view expected,
            const nlohmann::json& value)
        {
            if (expected == "object")
                return value.is_object();
            if (expected == "array")
                return value.is_array();
            if (expected == "string")
                return value.is_string();
            if (expected == "number")
                return value.is_number();
            if (expected == "integer")
                return value.is_number_integer() || value.is_number_unsigned();
            if (expected == "boolean")
                return value.is_boolean();
            if (expected == "null")
                return value.is_null();
            return true;
        }

        bool validate_schema(
            const nlohmann::json& schema,
            const nlohmann::json& value,
            std::string_view path,
            std::string& error)
        {
            if (!schema.is_object())
                return true;

            const auto type = schema.find("type");
            if (type != schema.end() && type->is_string())
            {
                const std::string& expected =
                    type->get_ref<const std::string&>();
                if (!matches_type(expected, value))
                {
                    error = std::string(path) +
                        " must be of type " + expected;
                    return false;
                }
            }

            const auto enumeration = schema.find("enum");
            if (enumeration != schema.end() && enumeration->is_array())
            {
                bool found = false;
                for (const nlohmann::json& candidate : *enumeration)
                {
                    if (candidate == value)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                {
                    error = std::string(path) +
                        " is not one of the allowed values";
                    return false;
                }
            }

            if (value.is_object())
            {
                const auto required = schema.find("required");
                if (required != schema.end() && required->is_array())
                {
                    for (const nlohmann::json& name : *required)
                    {
                        if (!name.is_string())
                            continue;
                        const std::string& key =
                            name.get_ref<const std::string&>();
                        if (!value.contains(key))
                        {
                            error = std::string(path) +
                                "." + key + " is required";
                            return false;
                        }
                    }
                }

                const auto properties = schema.find("properties");
                const bool reject_extra =
                    schema.value("additionalProperties", true) == false;

                for (auto iterator = value.begin();
                     iterator != value.end();
                     ++iterator)
                {
                    if (properties == schema.end()
                        || !properties->is_object()
                        || !properties->contains(iterator.key()))
                    {
                        if (reject_extra)
                        {
                            error = std::string(path) +
                                "." + iterator.key() +
                                " is not allowed";
                            return false;
                        }
                        continue;
                    }

                    if (!validate_schema(
                            properties->at(iterator.key()),
                            iterator.value(),
                            std::string(path) + "." + iterator.key(),
                            error))
                    {
                        return false;
                    }
                }
            }

            if (value.is_array())
            {
                const auto items = schema.find("items");
                if (items != schema.end())
                {
                    for (std::size_t index = 0; index < value.size(); ++index)
                    {
                        if (!validate_schema(
                                *items,
                                value.at(index),
                                std::string(path) +
                                    "[" + std::to_string(index) + "]",
                                error))
                        {
                            return false;
                        }
                    }
                }
            }

            return true;
        }

        nlohmann::json canonical_fallback(const nlohmann::json& source)
        {
            std::string id = synthesized_call_id();
            std::string name = "__invalid_tool_call__";
            std::string arguments = "{}";

            if (source.is_object())
            {
                const auto source_id = source.find("id");
                if (source_id != source.end()
                    && source_id->is_string()
                    && !source_id->get_ref<const std::string&>().empty())
                {
                    id = source_id->get<std::string>();
                }

                const auto function = source.find("function");
                if (function != source.end() && function->is_object())
                {
                    const auto source_name = function->find("name");
                    if (source_name != function->end()
                        && source_name->is_string()
                        && !source_name->get_ref<const std::string&>().empty())
                    {
                        name = source_name->get<std::string>();
                    }

                    const auto source_arguments = function->find("arguments");
                    if (source_arguments != function->end())
                    {
                        if (source_arguments->is_string())
                        {
                            const nlohmann::json parsed =
                                nlohmann::json::parse(
                                    source_arguments
                                        ->get_ref<const std::string&>(),
                                    nullptr,
                                    false);
                            arguments = parsed.is_discarded()
                                ? std::string("{}")
                                : parsed.dump();
                        }
                        else
                        {
                            arguments = source_arguments->dump();
                        }
                    }
                }
            }

            return {
                {"id", std::move(id)},
                {"type", "function"},
                {"function", {
                    {"name", std::move(name)},
                    {"arguments", std::move(arguments)}
                }}
            };
        }

        prepared_call prepare_call(const nlohmann::json& source)
        {
            prepared_call prepared;
            prepared.canonical = canonical_fallback(source);

            if (!source.is_object())
            {
                prepared.error = error_item(
                    "invalid_tool_call",
                    "Tool call must be an object");
                return prepared;
            }

            const auto function = source.find("function");
            if (function == source.end() || !function->is_object())
            {
                prepared.error = error_item(
                    "invalid_tool_call",
                    "Tool call function must be an object");
                return prepared;
            }

            const auto name = function->find("name");
            if (name == function->end()
                || !name->is_string()
                || name->get_ref<const std::string&>().empty())
            {
                prepared.error = error_item(
                    "tool_schema_not_found",
                    "Tool call has no function name");
                return prepared;
            }

            const std::string& tool_name =
                name->get_ref<const std::string&>();
            prepared.definition = find_definition(tool_name);
            if (prepared.definition == nullptr)
            {
                prepared.error = error_item(
                    "tool_schema_not_found",
                    "Tool is not present in tool_definitions: " + tool_name);
                return prepared;
            }

            prepared.canonical["function"]["name"] =
                prepared.definition->at("function").at("name");

            const auto type = source.find("type");
            if (type == source.end()
                || !type->is_string()
                || type->get_ref<const std::string&>() != "function")
            {
                prepared.error = error_item(
                    "invalid_tool_call_schema",
                    "Tool call type must be function");
                return prepared;
            }

            const auto source_arguments = function->find("arguments");
            if (source_arguments == function->end())
            {
                prepared.canonical["function"]["arguments"] = "{}";
                prepared.error = error_item(
                    "invalid_arguments",
                    "Tool call function.arguments is required");
                return prepared;
            }

            nlohmann::json parsed_arguments;
            if (source_arguments->is_string())
            {
                parsed_arguments = nlohmann::json::parse(
                    source_arguments->get_ref<const std::string&>(),
                    nullptr,
                    false);
                if (parsed_arguments.is_discarded())
                {
                    prepared.error = error_item(
                        "invalid_arguments",
                        "Tool call function.arguments contains invalid JSON");
                    return prepared;
                }
            }
            else
            {
                parsed_arguments = *source_arguments;
            }

            prepared.canonical["function"]["arguments"] =
                parsed_arguments.dump();

            const nlohmann::json& schema =
                prepared.definition->at("function").at("parameters");
            std::string validation_error;
            if (!validate_schema(
                    schema,
                    parsed_arguments,
                    "function.arguments",
                    validation_error))
            {
                prepared.error = error_item(
                    "invalid_arguments",
                    std::move(validation_error));
                return prepared;
            }

            return prepared;
        }

        nlohmann::json result_item_from_message(
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
                if (ok != payload.end()
                    && ok->is_boolean()
                    && !ok->get<bool>())
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

        nlohmann::json final_result_message(
            const nlohmann::json& canonical_call,
            const nlohmann::json* definition,
            nlohmann::json item)
        {
            const std::string& call_id =
                canonical_call.at("id").get_ref<const std::string&>();

            nlohmann::json results = nlohmann::json::array();
            results.push_back(std::move(item));

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

        execution_result make_execution_result(
            prepared_call prepared,
            nlohmann::json item)
        {
            nlohmann::json result = final_result_message(
                prepared.canonical,
                prepared.definition,
                std::move(item));
            return {
                std::move(prepared.canonical),
                std::move(result)
            };
        }
    }

    execution_result execute_invalid_json(std::string_view raw)
    {
        nlohmann::json source = {
            {"function", {
                {"arguments", std::string(raw)}
            }}
        };
        prepared_call prepared;
        prepared.canonical = canonical_fallback(source);
        return make_execution_result(
            std::move(prepared),
            error_item(
                "invalid_tool_call",
                "Tool call argument is not valid JSON"));
    }

    execution_result execute_tool(const nlohmann::json& input)
    {
        prepared_call prepared;
        try
        {
            prepared = prepare_call(input);
            if (prepared.error.has_value())
            {
                nlohmann::json item = std::move(*prepared.error);
                prepared.error.reset();
                return make_execution_result(
                    std::move(prepared),
                    std::move(item));
            }

            process_plan plan = dispatch_tool(prepared.canonical);
            if (!plan.process_required)
            {
                return make_execution_result(
                    std::move(prepared),
                    result_item_from_message(plan.immediate_result));
            }

            const process::result child = process::run(
                plan.executable,
                plan.arguments,
                plan.working_directory,
                plan.stdin_data);

            process_result_view view;
            view.started = child.started;
            view.exit_code = child.exit_code;
            view.final_error = child.error;
            view.stdout_text = child.stdout_text;
            view.stderr_text = child.stderr_text;

            normalized_result normalized =
                normalize_tool(prepared.canonical, view);

            return make_execution_result(
                std::move(prepared),
                result_item_from_message(normalized.json));
        }
        catch (const std::exception& exception)
        {
            if (prepared.canonical.is_null())
                prepared.canonical = canonical_fallback(input);

            return make_execution_result(
                std::move(prepared),
                error_item(
                    "tool_execution_error",
                    exception.what()));
        }
        catch (...)
        {
            if (prepared.canonical.is_null())
                prepared.canonical = canonical_fallback(input);

            return make_execution_result(
                std::move(prepared),
                error_item(
                    "tool_execution_error",
                    "Unknown tool runtime exception"));
        }
    }
}
