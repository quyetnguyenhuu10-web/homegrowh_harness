#include "runtime.h"

#include "response.h"
#include "../error/error.h"
#include "../platform/platform.h"

#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace tool_runtime::detail
{
    namespace
    {
        enum class Runtime { typescript, native };
        struct ToolSpec { Runtime runtime; };
        struct ToolCall
        {
            std::string id;
            std::string name;
            nlohmann::json arguments;
            nlohmann::json normalized;
        };

        std::mutex read_state_mutex;
        std::map<std::string, std::set<std::string>> read_files_by_workspace;

        const std::map<std::string, ToolSpec>& tool_specs()
        {
            static const std::map<std::string, ToolSpec> specs{
                {"read", {Runtime::typescript}},
                {"write", {Runtime::typescript}},
                {"glob", {Runtime::typescript}},
                {"grep", {Runtime::typescript}},
                {"webfetch", {Runtime::typescript}},
                {"todowrite", {Runtime::typescript}},
                {"edit_file", {Runtime::native}},
            };
            return specs;
        }

        nlohmann::json tool_result(const std::string& call_id, nlohmann::json&& payload)
        {
            return {
                {"role", "tool"},
                {"tool_call_id", call_id},
                {"content", payload.dump()}
            };
        }

        Result<ToolCall> parse_tool_call(const nlohmann::json& source)
        {
            const auto invalid = [&source](std::string_view path, std::string_view message)
            {
                return Result<ToolCall>::failure(make_error(
                    "parse_tool_call", "validation_error", message,
                    {{"path", path}, {"tool_call", source}}));
            };
            if (!source.is_object())
                return invalid("tool_call", "Tool call must be an object");
            const auto id = source.find("id");
            if (id == source.end() || !id->is_string() || id->get_ref<const std::string&>().empty())
                return invalid("tool_call.id", "Tool call id must be a non-empty string");
            const auto type = source.find("type");
            if (type == source.end() || !type->is_string() || *type != "function")
                return invalid("tool_call.type", "Tool call type must be function");
            const auto function = source.find("function");
            if (function == source.end() || !function->is_object())
                return invalid("tool_call.function", "Tool call function must be an object");
            const auto name = function->find("name");
            if (name == function->end() || !name->is_string() || name->get_ref<const std::string&>().empty())
                return invalid("tool_call.function.name", "Tool call function name must be a non-empty string");
            const auto arguments = function->find("arguments");
            if (arguments == function->end())
                return invalid("tool_call.function.arguments", "Tool call arguments are required");

            nlohmann::json parsed_arguments;
            if (arguments->is_string())
            {
                auto parsed = parse_json(arguments->get_ref<const std::string&>(), "parse_tool_call",
                    {{"path", "tool_call.function.arguments"}});
                if (parsed.error)
                    return Result<ToolCall>::failure(std::move(*parsed.error));
                parsed_arguments = std::move(*parsed.value);
            }
            else
                parsed_arguments = *arguments;
            if (!parsed_arguments.is_object() && !parsed_arguments.is_array())
                return invalid("tool_call.function.arguments", "Tool arguments must be an object or array");

            nlohmann::json normalized = source;
            normalized["function"]["arguments"] = parsed_arguments;
            return Result<ToolCall>::success(ToolCall{
                id->get<std::string>(), name->get<std::string>(),
                std::move(parsed_arguments), std::move(normalized)});
        }

        Result<std::filesystem::path> environment_path(const char* name)
        {
            auto text = environment_text(name);
            if (text.error)
                return Result<std::filesystem::path>::failure(std::move(*text.error));
            return Result<std::filesystem::path>::success(std::filesystem::path(std::move(*text.value)));
        }

        Result<std::filesystem::path> regular_file(
            const std::filesystem::path& path,
            std::string_view operation)
        {
            std::error_code code;
            const bool regular = std::filesystem::is_regular_file(path, code);
            if (code)
                return Result<std::filesystem::path>::failure(make_system_error(
                    operation, "std::filesystem::is_regular_file", code, {{"path", path_text(path)}}));
            if (!regular)
                return Result<std::filesystem::path>::failure(make_error(
                    operation, "validation_error", "Path is not a regular file", {{"path", path_text(path)}}));
            auto canonical = std::filesystem::canonical(path, code);
            if (code)
                return Result<std::filesystem::path>::failure(make_system_error(
                    operation, "std::filesystem::canonical", code, {{"path", path_text(path)}}));
            return Result<std::filesystem::path>::success(std::move(canonical));
        }

        Result<std::filesystem::path> find_on_path(std::string_view executable)
        {
            auto raw_path = environment_text("PATH");
            if (raw_path.error)
                return Result<std::filesystem::path>::failure(std::move(*raw_path.error));
            if (raw_path.value->empty())
                return Result<std::filesystem::path>::failure(make_error(
                    "find_executable", "configuration_error", "PATH is empty or not set",
                    {{"environment_variable", "PATH"}, {"executable", executable}}));
#if defined(_WIN32)
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif
            const auto& search_path = *raw_path.value;
            std::vector<Error> causes;
            std::size_t begin = 0;
            while (begin <= search_path.size())
            {
                const std::size_t end = search_path.find(separator, begin);
                const auto directory = search_path.substr(
                    begin, end == std::string::npos ? std::string::npos : end - begin);
                if (!directory.empty())
                {
                    auto candidate = regular_file(std::filesystem::path(directory) / executable, "find_executable");
                    if (candidate.value)
                        return candidate;
                    causes.push_back(std::move(*candidate.error));
                }
                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            return Result<std::filesystem::path>::failure(dependency_error(
                "find_executable", "Executable was not found on PATH", std::move(causes),
                {{"executable", executable}}));
        }

        Result<std::filesystem::path> node_executable()
        {
            auto executable = current_executable();
            if (executable.error)
                return Result<std::filesystem::path>::failure(std::move(*executable.error));
            auto bundled = regular_file(executable.value->parent_path()
#if defined(_WIN32)
                / "node.exe",
#else
                / "node",
#endif
                "resolve_node");
            if (bundled.value)
                return bundled;
            auto override_path = environment_path("HOMEGROWPH_NODE");
            if (override_path.error)
                return Result<std::filesystem::path>::failure(std::move(*override_path.error));
            if (!override_path.value->empty())
            {
                auto override_result = regular_file(*override_path.value, "resolve_node");
                if (override_result.error)
                    override_result.error->data.push_back({{"environment_variable", "HOMEGROWPH_NODE"}});
                return override_result;
            }
            auto path_result = find_on_path(
#if defined(_WIN32)
                "node.exe"
#else
                "node"
#endif
            );
            if (path_result.value)
                return path_result;
            std::vector<Error> causes;
            causes.push_back(std::move(*bundled.error));
            causes.push_back(std::move(*path_result.error));
            return Result<std::filesystem::path>::failure(dependency_error(
                "resolve_node", "Node executable resolution failed", std::move(causes)));
        }

        Result<std::filesystem::path> runtime_file(
            const char* override_variable,
            const std::filesystem::path& relative_path,
            std::string_view operation)
        {
            auto configured_path = environment_path(override_variable);
            if (configured_path.error)
                return Result<std::filesystem::path>::failure(std::move(*configured_path.error));
            auto path = std::move(*configured_path.value);
            const bool configured = !path.empty();
            if (!configured)
            {
                auto executable = current_executable();
                if (executable.error)
                    return Result<std::filesystem::path>::failure(std::move(*executable.error));
                path = executable.value->parent_path() / relative_path;
            }
            auto result = regular_file(path, operation);
            if (result.error && configured)
                result.error->data.push_back({{"environment_variable", override_variable}});
            return result;
        }

        nlohmann::json process_details(const process_result_view& result)
        {
            return {
                {"started", result.started},
                {"exit_code", result.exit_code},
                {"stdout", text_payload(result.stdout_text)},
                {"stderr", text_payload(result.stderr_text)}
            };
        }

        std::vector<std::string> read_files(const std::string& workspace_key)
        {
            std::lock_guard lock(read_state_mutex);
            const auto found = read_files_by_workspace.find(workspace_key);
            if (found == read_files_by_workspace.end())
                return {};
            return {found->second.begin(), found->second.end()};
        }

        std::optional<Error> merge_read_files(const std::string& workspace_key, const nlohmann::json& files)
        {
            if (!files.is_array())
                return make_error("merge_read_files", "protocol_error", "Worker readFiles must be an array",
                    {{"path", "readFiles"}, {"value", files}});
            for (std::size_t index = 0; index < files.size(); ++index)
            {
                if (!files[index].is_string())
                    return make_error("merge_read_files", "protocol_error", "Worker readFiles item must be a string",
                        {{"path", "readFiles[" + std::to_string(index) + "]"}, {"value", files[index]}});
            }
            std::lock_guard lock(read_state_mutex);
            std::set<std::string>& target = read_files_by_workspace[workspace_key];
            for (const auto& file : files)
                target.insert(file.get<std::string>());
            return std::nullopt;
        }

        Result<process_plan> dispatch_typescript(const ToolCall& call, const std::filesystem::path& workspace)
        {
            auto host = runtime_file("HOMEGROWPH_TOOL_HOST",
                std::filesystem::path("tool_runtime_tools") / "src" / "_runtime" / "tool_host.js", "resolve_tool_host");
            if (host.error)
                return Result<process_plan>::failure(std::move(*host.error));
            auto node = node_executable();
            if (node.error)
                return Result<process_plan>::failure(std::move(*node.error));
            const std::string workspace_key = path_text(workspace);
            nlohmann::json request = {
                {"version", 1}, {"tool", call.name}, {"toolCall", call.normalized},
                {"context", {{"repositoryPath", workspace_key}}},
                {"readFiles", read_files(workspace_key)}
            };
            process_plan plan;
            plan.executable = std::move(*node.value);
            plan.arguments = {"--preserve-symlinks", "--preserve-symlinks-main", path_text(*host.value)};
            plan.working_directory = workspace;
            plan.stdin_data = request.dump();
            return Result<process_plan>::success(std::move(plan));
        }

        Result<nlohmann::json> normalize_typescript(
            const ToolCall& call, const std::filesystem::path& workspace, const process_result_view& result)
        {
            auto parsed = parse_json(result.stdout_text, "parse_worker_response", process_details(result));
            if (parsed.error)
                return parsed;
            auto& response = *parsed.value;
            if (!response.is_object())
                return Result<nlohmann::json>::failure(make_error(
                    "normalize_worker_response", "protocol_error", "Worker response must be an object",
                    {{"response", response}, {"process", process_details(result)}}));
            const auto ok = response.find("ok");
            if (ok == response.end() || !ok->is_boolean())
                return Result<nlohmann::json>::failure(make_error(
                    "normalize_worker_response", "protocol_error", "Worker response must contain a boolean ok field",
                    {{"response", response}, {"process", process_details(result)}}));
            if (!ok->get<bool>())
            {
                auto failure = normalize_result_payload(std::move(response), call.name, "invoke");
                if (failure.error)
                    return failure;
                return Result<nlohmann::json>::failure(make_error(
                    "normalize_worker_response", "protocol_error", "Worker reported failure without an error"));
            }
            const auto reported = response.find("error");
            if (reported != response.end() && !reported->is_null())
                return normalize_result_payload(std::move(response), call.name, "invoke");
            const auto message = response.find("result");
            const auto files = response.find("readFiles");
            if (message == response.end() || !message->is_object() || files == response.end())
                return Result<nlohmann::json>::failure(make_error(
                    "normalize_worker_response", "protocol_error", "Worker response is incomplete",
                    {{"response", response}, {"process", process_details(result)}}));
            auto content = normalize_result_message(*message, call.name, "invoke");
            auto files_error = merge_read_files(path_text(workspace), *files);
            if (content.error && files_error)
            {
                std::vector<Error> causes;
                causes.push_back(std::move(*content.error));
                causes.push_back(std::move(*files_error));
                return Result<nlohmann::json>::failure(dependency_error(
                    "normalize_worker_response", "Worker result and read state both failed", std::move(causes)));
            }
            if (content.error)
                return content;
            if (files_error)
                return Result<nlohmann::json>::failure(std::move(*files_error));
            return Result<nlohmann::json>::success(nlohmann::json(*message));
        }

        Result<process_plan> dispatch_native(const ToolCall& call, const std::filesystem::path& workspace)
        {
            if (call.name != "edit_file")
                return Result<process_plan>::failure(make_error(
                    "dispatch_tool", "configuration_error", "Native tool is not implemented", {{"tool", call.name}}));
            nlohmann::json requests = nlohmann::json::array();
            if (call.arguments.is_array())
            {
                for (std::size_t index = 0; index < call.arguments.size(); ++index)
                {
                    if (!call.arguments[index].is_object())
                        return Result<process_plan>::failure(make_error(
                            "dispatch_tool", "validation_error", "Tool arguments array must contain only objects",
                            {{"path", "function.arguments[" + std::to_string(index) + "]"}, {"value", call.arguments[index]}}));
                    requests.push_back(call.arguments[index]);
                }
            }
            else
                requests.push_back(call.arguments);
            if (requests.empty())
                return Result<process_plan>::failure(make_error(
                    "dispatch_tool", "validation_error", "Tool arguments must not be empty", {{"tool", call.name}}));
            nlohmann::json envelope = {
                {"version", 1}, {"tool", call.name}, {"call_id", call.id},
                {"arguments", {{"requests", std::move(requests)}}}
            };
            auto executable = runtime_file("HOMEGROWPH_EDIT_FILE",
#if defined(_WIN32)
                "edit_file.exe",
#else
                "edit_file",
#endif
                "resolve_native_tool");
            if (executable.error)
                return Result<process_plan>::failure(std::move(*executable.error));
            process_plan plan;
            plan.executable = std::move(*executable.value);
            plan.arguments = {"--toolcall-stdin"};
            plan.working_directory = workspace;
            plan.stdin_data = envelope.dump();
            return Result<process_plan>::success(std::move(plan));
        }

        Result<nlohmann::json> normalize_native(const ToolCall& call, const process_result_view& result)
        {
            auto parsed = parse_json(result.stdout_text, "parse_native_response", process_details(result));
            if (parsed.error)
                return parsed;
            auto payload = normalize_result_payload(std::move(*parsed.value), call.name, "invoke");
            if (payload.error)
                return payload;
            return Result<nlohmann::json>::success(tool_result(call.id, std::move(*payload.value)));
        }
    }

    Result<process_plan> dispatch_tool(const nlohmann::json& input)
    {
        try
        {
            auto parsed = parse_tool_call(input);
            if (parsed.error)
                return Result<process_plan>::failure(std::move(*parsed.error));
            auto workspace = current_workspace();
            if (workspace.error)
                return Result<process_plan>::failure(std::move(*workspace.error));
            const ToolCall& call = *parsed.value;
            const auto spec = tool_specs().find(call.name);
            if (spec == tool_specs().end())
                return Result<process_plan>::failure(make_error(
                    "dispatch_tool", "configuration_error", "Tool is not registered", {{"tool", call.name}}));
            return spec->second.runtime == Runtime::typescript
                ? dispatch_typescript(call, *workspace.value)
                : dispatch_native(call, *workspace.value);
        }
        catch (...)
        {
            return Result<process_plan>::failure(current_exception_error("dispatch_tool"));
        }
    }

    Result<normalized_result> normalize_tool(const nlohmann::json& input, const process_result_view& result)
    {
        try
        {
            auto parsed = parse_tool_call(input);
            if (parsed.error)
                return Result<normalized_result>::failure(std::move(*parsed.error));
            const ToolCall& call = *parsed.value;
            if (result.error && result.stdout_text.empty())
            {
                std::vector<Error> causes{*result.error};
                return Result<normalized_result>::failure(dependency_error(
                    "run_tool", "Tool process failed", std::move(causes),
                    {{"tool", call.name}, {"process", process_details(result)}}));
            }
            const auto spec = tool_specs().find(call.name);
            if (spec == tool_specs().end())
                return Result<normalized_result>::failure(make_error(
                    "normalize_tool", "configuration_error", "Tool is not registered", {{"tool", call.name}}));
            Result<nlohmann::json> normalized;
            if (spec->second.runtime == Runtime::typescript)
            {
                auto workspace = current_workspace();
                if (workspace.error)
                    return Result<normalized_result>::failure(std::move(*workspace.error));
                normalized = normalize_typescript(call, *workspace.value, result);
            }
            else
                normalized = normalize_native(call, result);
            if (result.error)
            {
                std::vector<Error> causes{*result.error};
                if (normalized.error)
                    causes.push_back(std::move(*normalized.error));
                return Result<normalized_result>::failure(dependency_error(
                    "run_tool", "Tool process failed", std::move(causes),
                    {{"tool", call.name}, {"process", process_details(result)}}));
            }
            if (!result.started)
            {
                Error error = make_error("run_tool", "process_error", "Tool process was not started",
                    {{"tool", call.name}, {"process", process_details(result)}});
                if (normalized.error)
                    error.causes.push_back(std::move(*normalized.error));
                return Result<normalized_result>::failure(std::move(error));
            }
            if (normalized.error)
            {
                if (result.exit_code != 0)
                {
                    std::vector<Error> causes;
                    causes.push_back(std::move(*normalized.error));
                    return Result<normalized_result>::failure(dependency_error(
                        "run_tool", "Tool process exited unsuccessfully", std::move(causes),
                        {{"tool", call.name}, {"process", process_details(result)}}));
                }
                return Result<normalized_result>::failure(std::move(*normalized.error));
            }
            if (result.exit_code != 0)
                return Result<normalized_result>::failure(make_error(
                    "run_tool", "process_error", "Tool process exited unsuccessfully",
                    {{"tool", call.name}, {"process", process_details(result)}, {"response", *normalized.value}}));
            return Result<normalized_result>::success(normalized_result{std::move(*normalized.value), result.stderr_text});
        }
        catch (...)
        {
            return Result<normalized_result>::failure(current_exception_error("normalize_tool"));
        }
    }
}

