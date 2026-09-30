#include "runtime.h"

#include "response.h"
#include "../error/error.h"
#include "../platform/platform.h"
#include "../process/process.h"

#include <chrono>
#include <cstdlib>
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
            std::optional<Error> error;
        };

        std::string synthesized_call_id()
        {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#if defined(_WIN32)
            const auto process_id = GetCurrentProcessId();
#else
            const auto process_id = ::getpid();
#endif
            return "hh_tool_call_" + std::to_string(process_id) + "_" + std::to_string(stamp);
        }

        Result<std::filesystem::path> tool_definitions_path()
        {
            auto path = environment_text("HOMEGROWPH_TOOL_DEFINITIONS");
            if (path.error)
                return Result<std::filesystem::path>::failure(std::move(*path.error));
            if (!path.value->empty())
                return Result<std::filesystem::path>::success(std::filesystem::path(std::move(*path.value)));
            auto executable = current_executable();
            if (executable.error)
                return Result<std::filesystem::path>::failure(std::move(*executable.error));
            return Result<std::filesystem::path>::success(
                executable.value->parent_path() / "tool_runtime_tools" / "tool_definitions.json");
        }

        const Result<nlohmann::json>& tool_definitions()
        {
            static const Result<nlohmann::json> definitions = []
            {
                try
                {
                    auto path = tool_definitions_path();
                    if (path.error)
                        return Result<nlohmann::json>::failure(std::move(*path.error));
                    auto parsed = read_json_file(*path.value, "load_tool_definitions");
                    if (parsed.error)
                        return parsed;
                    if (!parsed.value->is_array())
                        return Result<nlohmann::json>::failure(make_error(
                            "load_tool_definitions", "protocol_error", "Tool definitions must be an array",
                            {{"path", path_text(*path.value)}, {"value", *parsed.value}}));
                    for (std::size_t index = 0; index < parsed.value->size(); ++index)
                    {
                        const auto& item = parsed.value->at(index);
                        const auto invalid = [&]
                        {
                            return Result<nlohmann::json>::failure(make_error(
                                "load_tool_definitions", "protocol_error", "Tool definition must contain a function name and parameters schema",
                                {{"path", path_text(*path.value)}, {"index", index}, {"value", item}}));
                        };
                        if (!item.is_object())
                            return invalid();
                        const auto function = item.find("function");
                        if (function == item.end() || !function->is_object())
                            return invalid();
                        const auto name = function->find("name");
                        const auto parameters = function->find("parameters");
                        if (name == function->end() || !name->is_string()
                            || name->get_ref<const std::string&>().empty()
                            || parameters == function->end() || !parameters->is_object())
                            return invalid();
                    }
                    return parsed;
                }
                catch (...)
                {
                    return Result<nlohmann::json>::failure(current_exception_error("load_tool_definitions"));
                }
            }();
            return definitions;
        }

        Result<const nlohmann::json*> find_definition(std::string_view name)
        {
            const auto& definitions = tool_definitions();
            if (definitions.error)
                return Result<const nlohmann::json*>::failure(Error(*definitions.error));
            for (const auto& item : *definitions.value)
            {
                const auto& function = item.at("function");
                if (function.at("name").get_ref<const std::string&>() == name)
                    return Result<const nlohmann::json*>::success(&item);
            }
            return Result<const nlohmann::json*>::failure(make_error(
                "find_tool_definition", "configuration_error", "Tool is not present in tool definitions", {{"tool", name}}));
        }

        nlohmann::json error_item(Error&& error)
        {
            return {{"ok", false}, {"value", nullptr}, {"error", std::move(error)}};
        }

        nlohmann::json value_item(nlohmann::json&& value)
        {
            return {{"ok", true}, {"value", std::move(value)}, {"error", nullptr}};
        }

        bool matches_type(std::string_view expected, const nlohmann::json& value)
        {
            if (expected == "object") return value.is_object();
            if (expected == "array") return value.is_array();
            if (expected == "string") return value.is_string();
            if (expected == "number") return value.is_number();
            if (expected == "integer") return value.is_number_integer() || value.is_number_unsigned();
            if (expected == "boolean") return value.is_boolean();
            if (expected == "null") return value.is_null();
            return true;
        }

        void validate_schema(
            const nlohmann::json& schema,
            const nlohmann::json& value,
            const std::string& path,
            std::vector<Error>& errors)
        {
            if (!schema.is_object())
                return;
            const auto violation = [&](std::string_view keyword, std::string_view message, const nlohmann::json& expected)
            {
                errors.push_back(make_error("validate_arguments", "validation_error", message,
                    {{"path", path}, {"keyword", keyword}, {"expected", expected}, {"value", value}}));
            };
            const auto type = schema.find("type");
            if (type != schema.end() && type->is_string() && !matches_type(type->get_ref<const std::string&>(), value))
            {
                violation("type", "Argument has an unexpected type", *type);
                return;
            }
            const auto enumeration = schema.find("enum");
            if (enumeration != schema.end() && enumeration->is_array())
            {
                bool found = false;
                for (const auto& candidate : *enumeration)
                {
                    if (candidate == value)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found)
                    violation("enum", "Argument is not one of the allowed values", *enumeration);
            }
            if (value.is_object())
            {
                const auto required = schema.find("required");
                if (required != schema.end() && required->is_array())
                {
                    for (const auto& name : *required)
                    {
                        if (!name.is_string())
                            continue;
                        const auto& key = name.get_ref<const std::string&>();
                        if (!value.contains(key))
                            errors.push_back(make_error("validate_arguments", "validation_error", "Required argument is missing",
                                {{"path", path + "." + key}, {"keyword", "required"}, {"value", value}}));
                    }
                }
                const auto properties = schema.find("properties");
                const auto additional = schema.find("additionalProperties");
                const bool reject_extra = additional != schema.end()
                    && additional->is_boolean() && !additional->get<bool>();
                for (auto item = value.begin(); item != value.end(); ++item)
                {
                    if (properties == schema.end() || !properties->is_object() || !properties->contains(item.key()))
                    {
                        if (reject_extra)
                            errors.push_back(make_error("validate_arguments", "validation_error", "Additional argument is not allowed",
                                {{"path", path + "." + item.key()}, {"keyword", "additionalProperties"}, {"value", item.value()}}));
                        continue;
                    }
                    validate_schema(properties->at(item.key()), item.value(), path + "." + item.key(), errors);
                }
            }
            if (value.is_array())
            {
                const auto items = schema.find("items");
                if (items != schema.end())
                {
                    for (std::size_t index = 0; index < value.size(); ++index)
                        validate_schema(*items, value[index], path + "[" + std::to_string(index) + "]", errors);
                }
            }
        }

        nlohmann::json canonical_fallback(const nlohmann::json& source)
        {
            std::string id = synthesized_call_id();
            std::string name = "__invalid_tool_call__";
            std::string arguments = "{}";
            if (source.is_object())
            {
                const auto source_id = source.find("id");
                if (source_id != source.end() && source_id->is_string() && !source_id->get_ref<const std::string&>().empty())
                    id = source_id->get<std::string>();
                const auto function = source.find("function");
                if (function != source.end() && function->is_object())
                {
                    const auto source_name = function->find("name");
                    if (source_name != function->end() && source_name->is_string() && !source_name->get_ref<const std::string&>().empty())
                        name = source_name->get<std::string>();
                    const auto source_arguments = function->find("arguments");
                    if (source_arguments != function->end())
                    {
                        if (source_arguments->is_string())
                        {
                            auto parsed = parse_json(source_arguments->get_ref<const std::string&>(), "parse_arguments");
                            if (parsed.value)
                                arguments = parsed.value->dump();
                        }
                        else
                            arguments = source_arguments->dump();
                    }
                }
            }
            return {
                {"id", std::move(id)}, {"type", "function"},
                {"function", {{"name", std::move(name)}, {"arguments", std::move(arguments)}}}
            };
        }

        prepared_call prepare_call(const nlohmann::json& source)
        {
            prepared_call prepared;
            prepared.canonical = canonical_fallback(source);
            const auto invalid = [&prepared, &source](std::string_view path, std::string_view message)
            {
                prepared.error = make_error("prepare_tool_call", "validation_error", message,
                    {{"path", path}, {"tool_call", source}});
            };
            if (!source.is_object())
            {
                invalid("tool_call", "Tool call must be an object");
                return prepared;
            }
            const auto function = source.find("function");
            if (function == source.end() || !function->is_object())
            {
                invalid("function", "Tool call function must be an object");
                return prepared;
            }
            const auto name = function->find("name");
            if (name == function->end() || !name->is_string() || name->get_ref<const std::string&>().empty())
            {
                invalid("function.name", "Tool call must contain a function name");
                return prepared;
            }
            auto definition = find_definition(name->get_ref<const std::string&>());
            if (definition.error)
            {
                prepared.error = std::move(definition.error);
                return prepared;
            }
            prepared.definition = *definition.value;
            const auto type = source.find("type");
            if (type == source.end() || !type->is_string() || *type != "function")
            {
                invalid("type", "Tool call type must be function");
                return prepared;
            }
            const auto arguments = function->find("arguments");
            if (arguments == function->end())
            {
                invalid("function.arguments", "Tool call arguments are required");
                return prepared;
            }
            nlohmann::json parsed_arguments;
            if (arguments->is_string())
            {
                auto parsed = parse_json(arguments->get_ref<const std::string&>(), "parse_arguments",
                    {{"path", "function.arguments"}});
                if (parsed.error)
                {
                    prepared.error = std::move(parsed.error);
                    return prepared;
                }
                parsed_arguments = std::move(*parsed.value);
            }
            else
                parsed_arguments = *arguments;
            prepared.canonical["function"]["arguments"] = parsed_arguments.dump();
            std::vector<Error> errors;
            validate_schema(prepared.definition->at("function").at("parameters"), parsed_arguments, "function.arguments", errors);
            if (errors.size() == 1)
                prepared.error = std::move(errors.front());
            else if (!errors.empty())
            {
                prepared.error = make_error("validate_arguments", "validation_error", "Tool arguments do not match the schema",
                    {{"tool", name->get_ref<const std::string&>()}});
                prepared.error->causes = std::move(errors);
            }
            return prepared;
        }

        nlohmann::json result_item_from_message(const nlohmann::json& message)
        {
            auto normalized = normalize_result_message(message, "tool_worker", "invoke");
            return normalized.error ? error_item(std::move(*normalized.error)) : value_item(std::move(*normalized.value));
        }

        execution_result make_execution_result(prepared_call&& prepared, nlohmann::json&& item)
        {
            const std::string& call_id = prepared.canonical.at("id").get_ref<const std::string&>();
            nlohmann::json results = nlohmann::json::array();
            results.push_back(std::move(item));
            const nlohmann::json envelope = {
                {"version", 1}, {"tool", prepared.definition == nullptr ? nlohmann::json(nullptr) : *prepared.definition},
                {"call_id", call_id}, {"results", std::move(results)}
            };
            nlohmann::json result = {
                {"role", "tool"}, {"tool_call_id", call_id}, {"content", envelope.dump()}
            };
            return {std::move(prepared.canonical), std::move(result)};
        }
    }

    execution_result execute_error(const nlohmann::json& input, Error&& error)
    {
        prepared_call prepared;
        prepared.canonical = canonical_fallback(input);
        return make_execution_result(std::move(prepared), error_item(std::move(error)));
    }

    execution_result execute_invalid_json(std::string_view raw)
    {
        auto parsed = parse_json(raw, "parse_input", {{"api", "nlohmann::json::parse"}});
        if (parsed.error)
            return execute_error(nlohmann::json::object(), std::move(*parsed.error));
        return execute_tool(*parsed.value);
    }

    execution_result execute_tool(const nlohmann::json& input)
    {
        prepared_call prepared;
        try
        {
            prepared = prepare_call(input);
            if (prepared.error)
                return make_execution_result(std::move(prepared), error_item(std::move(*prepared.error)));
            auto plan = dispatch_tool(prepared.canonical);
            if (plan.error)
                return make_execution_result(std::move(prepared), error_item(std::move(*plan.error)));
            process::result child = process::run(
                plan.value->executable, plan.value->arguments, plan.value->working_directory, plan.value->stdin_data);
            process_result_view view;
            view.started = child.started;
            view.exit_code = child.exit_code;
            view.error = std::move(child.error);
            view.stdout_text = std::move(child.stdout_text);
            view.stderr_text = std::move(child.stderr_text);
            auto normalized = normalize_tool(prepared.canonical, view);
            if (normalized.error)
                return make_execution_result(std::move(prepared), error_item(std::move(*normalized.error)));
            return make_execution_result(std::move(prepared), result_item_from_message(normalized.value->json));
        }
        catch (...)
        {
            if (prepared.canonical.is_null())
                prepared.canonical = canonical_fallback(input);
            return make_execution_result(std::move(prepared), error_item(current_exception_error("execute_tool")));
        }
    }
}

