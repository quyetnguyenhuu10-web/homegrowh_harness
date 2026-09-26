#include "runtime.h"

#include <sandbox/process.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#ifndef TOOL_RUNTIME_TOOL_HOST_PATH
#error "TOOL_RUNTIME_TOOL_HOST_PATH is not defined"
#endif

#ifndef TOOL_RUNTIME_EDIT_FILE_PATH
#error "TOOL_RUNTIME_EDIT_FILE_PATH is not defined"
#endif

namespace tool_runtime::detail
{
    namespace
    {
        enum class Runtime
        {
            typescript,
            native
        };

        struct ToolSpec
        {
            Runtime runtime;
            std::optional<sandbox::permission> filesystem;
            sandbox::network_access network = sandbox::network_access::none;
        };

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
                {"read", {
                    Runtime::typescript,
                    sandbox::permission::read_only,
                    sandbox::network_access::none}},
                {"write", {
                    Runtime::typescript,
                    sandbox::permission::read_write,
                    sandbox::network_access::none}},
                {"glob", {
                    Runtime::typescript,
                    sandbox::permission::read_only,
                    sandbox::network_access::none}},
                {"grep", {
                    Runtime::typescript,
                    sandbox::permission::read_only,
                    sandbox::network_access::none}},
                {"webfetch", {
                    Runtime::typescript,
                    std::nullopt,
                    sandbox::network_access::internet_client}},
                {"todowrite", {
                    Runtime::typescript,
                    std::nullopt,
                    sandbox::network_access::none}},
                {"edit_file", {
                    Runtime::native,
                    sandbox::permission::read_write,
                    sandbox::network_access::none}},
            };
            return specs;
        }

        std::string path_text(const std::filesystem::path& path)
        {
            const std::u8string value = path.u8string();
            return std::string(
                reinterpret_cast<const char*>(value.data()),
                value.size());
        }

        std::filesystem::path canonical_existing_directory(
            const std::filesystem::path& path)
        {
            if (path.empty())
                throw std::invalid_argument("tool_runtime: workspace_path is empty");

            std::error_code error;
            if (!std::filesystem::is_directory(path, error))
            {
                if (error)
                    throw std::filesystem::filesystem_error(
                        "tool_runtime: cannot inspect workspace",
                        path,
                        error);

                throw std::filesystem::filesystem_error(
                    "tool_runtime: workspace is not a directory",
                    path,
                    std::make_error_code(std::errc::not_a_directory));
            }

            const std::filesystem::path canonical =
                std::filesystem::canonical(path, error);
            if (error)
            {
                throw std::filesystem::filesystem_error(
                    "tool_runtime: cannot canonicalize workspace",
                    path,
                    error);
            }
            return canonical;
        }

        nlohmann::json tool_result(
            const std::string& call_id,
            nlohmann::json payload)
        {
            return {
                {"role", "tool"},
                {"tool_call_id", call_id},
                {"content", std::move(payload).dump()}
            };
        }

        nlohmann::json error_result(
            const std::string& call_id,
            const std::string& tool,
            const std::string& code,
            const std::string& message,
            nlohmann::json details = nlohmann::json::object())
        {
            nlohmann::json error = {
                {"code", code},
                {"message", message}
            };
            for (auto& [key, value] : details.items())
                error[key] = std::move(value);

            return tool_result(
                call_id,
                {
                    {"version", 1},
                    {"tool", tool},
                    {"call_id", call_id},
                    {"ok", false},
                    {"results", nlohmann::json::array()},
                    {"error", std::move(error)}
                });
        }

        ToolCall parse_tool_call(const nlohmann::json& source)
        {
            if (!source.is_object())
                throw std::invalid_argument("tool_runtime: tool_call must be an object");

            const auto id = source.find("id");
            if (id == source.end() || !id->is_string() || id->get_ref<const std::string&>().empty())
                throw std::invalid_argument("tool_runtime: tool_call.id must be a non-empty string");

            const auto type = source.find("type");
            if (type == source.end() || !type->is_string() || *type != "function")
                throw std::invalid_argument("tool_runtime: tool_call.type must be function");

            const auto function = source.find("function");
            if (function == source.end() || !function->is_object())
                throw std::invalid_argument("tool_runtime: tool_call.function must be an object");

            const auto name = function->find("name");
            if (name == function->end() || !name->is_string() || name->get_ref<const std::string&>().empty())
                throw std::invalid_argument("tool_runtime: tool_call.function.name must be a non-empty string");

            const auto arguments = function->find("arguments");
            if (arguments == function->end())
                throw std::invalid_argument("tool_runtime: tool_call.function.arguments is required");

            nlohmann::json parsed_arguments;
            if (arguments->is_string())
            {
                parsed_arguments = nlohmann::json::parse(
                    arguments->get_ref<const std::string&>(),
                    nullptr,
                    false);
                if (parsed_arguments.is_discarded())
                    throw std::invalid_argument(
                        "tool_runtime: tool_call.function.arguments contains invalid JSON");
            }
            else
            {
                parsed_arguments = *arguments;
            }

            if (!parsed_arguments.is_object() && !parsed_arguments.is_array())
            {
                throw std::invalid_argument(
                    "tool_runtime: tool_call.function.arguments must be an object or array");
            }

            nlohmann::json normalized = source;
            normalized["function"]["arguments"] = parsed_arguments;

            return ToolCall{
                id->get<std::string>(),
                name->get<std::string>(),
                std::move(parsed_arguments),
                std::move(normalized)};
        }

        std::filesystem::path environment_path(const char* name)
        {
            const char* value = std::getenv(name);
            if (value == nullptr || *value == '\0')
                return {};
            return std::filesystem::path(value);
        }

        std::filesystem::path find_on_path(std::string_view executable)
        {
            const char* raw_path = std::getenv("PATH");
            if (raw_path == nullptr)
                return {};

#if defined(_WIN32)
            constexpr char separator = ';';
#else
            constexpr char separator = ':';
#endif

            const std::string search_path(raw_path);
            std::size_t begin = 0;
            while (begin <= search_path.size())
            {
                const std::size_t end = search_path.find(separator, begin);
                const std::string directory = search_path.substr(
                    begin,
                    end == std::string::npos
                        ? std::string::npos
                        : end - begin);

                if (!directory.empty())
                {
                    std::filesystem::path candidate =
                        std::filesystem::path(directory) / executable;
                    std::error_code error;
                    if (std::filesystem::is_regular_file(candidate, error))
                    {
                        const std::filesystem::path canonical =
                            std::filesystem::canonical(candidate, error);
                        return error ? candidate : canonical;
                    }
                }

                if (end == std::string::npos)
                    break;
                begin = end + 1;
            }
            return {};
        }

        std::filesystem::path node_executable()
        {
            std::filesystem::path node = environment_path("HOMEGROWPH_NODE");
            if (!node.empty())
            {
                std::error_code error;
                if (!std::filesystem::is_regular_file(node, error))
                    throw std::runtime_error(
                        "tool_runtime: HOMEGROWPH_NODE does not point to a file");
                const std::filesystem::path canonical =
                    std::filesystem::canonical(node, error);
                return error ? node : canonical;
            }

#if defined(_WIN32)
            node = find_on_path("node.exe");
#else
            node = find_on_path("node");
#endif
            if (node.empty())
                throw std::runtime_error("tool_runtime: node executable not found");
            return node;
        }

        std::filesystem::path tool_host_path()
        {
            std::filesystem::path path =
                environment_path("HOMEGROWPH_TOOL_HOST");
            if (path.empty())
                path = std::filesystem::path(TOOL_RUNTIME_TOOL_HOST_PATH);

            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error))
            {
                throw std::runtime_error(
                    "tool_runtime: TypeScript tool host not found: " +
                    path_text(path));
            }

            const std::filesystem::path canonical =
                std::filesystem::canonical(path, error);
            return error ? path : canonical;
        }

        std::filesystem::path edit_file_path()
        {
            std::filesystem::path path =
                environment_path("HOMEGROWPH_EDIT_FILE");
            if (path.empty())
                path = std::filesystem::path(TOOL_RUNTIME_EDIT_FILE_PATH);

            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error))
            {
                throw std::runtime_error(
                    "tool_runtime: edit_file executable not found: " +
                    path_text(path));
            }

            const std::filesystem::path canonical =
                std::filesystem::canonical(path, error);
            return error ? path : canonical;
        }

        nlohmann::json process_details(const sandbox::process_result& result)
        {
            nlohmann::json path_errors = nlohmann::json::array();
            for (const sandbox::registry_path_error& item :
                 result.state.registry.path_errors)
            {
                path_errors.push_back({
                    {"path", path_text(item.path)},
                    {"code", item.error.value()},
                    {"message", item.error.message()}
                });
            }

            return {
                {"started", result.state.started},
                {"timed_out", result.state.timed_out},
                {"terminated", result.state.terminated},
                {"exit_code", result.state.exit_code},
                {"os_error_before_termination", {
                    {"code", result.state.os_error_before_termination.value()},
                    {"message", result.state.os_error_before_termination.message()}
                }},
                {"final_error", {
                    {"code", result.state.final_error.value()},
                    {"message", result.state.final_error.message()}
                }},
                {"registry_final_error", {
                    {"code", result.state.registry.final_error.value()},
                    {"message", result.state.registry.final_error.message()}
                }},
                {"path_errors", std::move(path_errors)}
            };
        }

        std::optional<nlohmann::json> process_failure(
            const ToolCall& call,
            const sandbox::process_result& result)
        {
            if (result.state.timed_out)
            {
                return error_result(
                    call.id,
                    call.name,
                    "process_timeout",
                    "Tool process timed out",
                    process_details(result));
            }

            if (result.state.registry.final_error
                || (!result.state.started
                    && !result.state.registry.path_errors.empty()))
            {
                return error_result(
                    call.id,
                    call.name,
                    "sandbox_registry_failed",
                    "Sandbox registry failed",
                    process_details(result));
            }

            if (result.state.final_error)
            {
                return error_result(
                    call.id,
                    call.name,
                    "sandbox_process_failed",
                    "Sandbox process failed",
                    process_details(result));
            }

            return std::nullopt;
        }

        void add_filesystem(
            std::vector<sandbox::registry_request>& filesystem,
            const std::filesystem::path& path,
            sandbox::permission permission)
        {
            const auto found = std::find_if(
                filesystem.begin(),
                filesystem.end(),
                [&](const sandbox::registry_request& item)
                {
                    return item.path == path;
                });

            if (found == filesystem.end())
            {
                filesystem.push_back({path, permission});
                return;
            }

            if (permission == sandbox::permission::read_write)
                found->access = sandbox::permission::read_write;
        }

        std::vector<std::string> read_files(const std::string& workspace_key)
        {
            std::lock_guard lock(read_state_mutex);
            const auto found = read_files_by_workspace.find(workspace_key);
            if (found == read_files_by_workspace.end())
                return {};
            return {found->second.begin(), found->second.end()};
        }

        void merge_read_files(
            const std::string& workspace_key,
            const nlohmann::json& files)
        {
            if (!files.is_array())
                throw std::runtime_error(
                    "tool_runtime: TypeScript worker readFiles must be an array");

            std::lock_guard lock(read_state_mutex);
            std::set<std::string>& target =
                read_files_by_workspace[workspace_key];
            for (const nlohmann::json& file : files)
            {
                if (!file.is_string())
                    throw std::runtime_error(
                        "tool_runtime: TypeScript worker readFiles item must be a string");
                target.insert(file.get<std::string>());
            }
        }

        nlohmann::json execute_typescript(
            const ToolCall& call,
            const ToolSpec& spec,
            const std::filesystem::path& workspace,
            bool refresh,
            std::uint32_t timeout_ms)
        {
            const std::filesystem::path host = tool_host_path();
            const std::filesystem::path runtime_root =
                host.parent_path().parent_path();
            const std::filesystem::path package_json =
                runtime_root.parent_path().parent_path() / "package.json";
            const std::filesystem::path node = node_executable();
            const std::string workspace_key = path_text(workspace);

            nlohmann::json request = {
                {"version", 1},
                {"tool", call.name},
                {"toolCall", call.normalized},
                {"context", {
                    {"repositoryPath", workspace_key}
                }},
                {"readFiles", read_files(workspace_key)}
            };

            sandbox::process_request process;
            process.executable = node;
            process.arguments = {
                "--preserve-symlinks",
                "--preserve-symlinks-main",
                path_text(host)
            };
            process.working_directory =
                spec.filesystem.has_value() ? workspace : runtime_root;
            process.stdin_data = request.dump();
            process.timeout = std::chrono::milliseconds(timeout_ms);
            process.network = spec.network;
            process.refresh = refresh;

            add_filesystem(
                process.filesystem,
                runtime_root,
                sandbox::permission::read_only);
            add_filesystem(
                process.filesystem,
                package_json,
                sandbox::permission::read_only);
            add_filesystem(
                process.filesystem,
                node.parent_path(),
                sandbox::permission::read_only);

            if (spec.filesystem.has_value())
                add_filesystem(process.filesystem, workspace, *spec.filesystem);

            const sandbox::process_result result = sandbox::process(process);
            if (const auto failure = process_failure(call, result))
                return *failure;

            const nlohmann::json response = nlohmann::json::parse(
                result.stdout_text,
                nullptr,
                false);
            if (response.is_discarded() || !response.is_object())
            {
                return error_result(
                    call.id,
                    call.name,
                    "invalid_tool_output",
                    "TypeScript tool worker returned invalid JSON",
                    {
                        {"exit_code", result.state.exit_code},
                        {"stderr", result.stderr_text}
                    });
            }

            const auto ok = response.find("ok");
            if (ok == response.end() || !ok->is_boolean())
            {
                return error_result(
                    call.id,
                    call.name,
                    "invalid_tool_output",
                    "TypeScript tool worker response has no boolean ok field");
            }

            if (!ok->get<bool>())
            {
                const auto error = response.find("error");
                if (error != response.end() && error->is_object())
                {
                    const std::string message = error->value(
                        "message",
                        std::string("Tool execution failed"));
                    return error_result(
                        call.id,
                        call.name,
                        "tool_execution_error",
                        message,
                        {{"source_error", *error}});
                }

                return error_result(
                    call.id,
                    call.name,
                    "tool_execution_error",
                    error != response.end() && error->is_string()
                        ? error->get<std::string>()
                        : std::string("Tool execution failed"));
            }

            const auto tool_result_message = response.find("result");
            const auto files = response.find("readFiles");
            if (tool_result_message == response.end()
                || !tool_result_message->is_object()
                || files == response.end())
            {
                return error_result(
                    call.id,
                    call.name,
                    "invalid_tool_output",
                    "TypeScript tool worker response is incomplete");
            }

            merge_read_files(workspace_key, *files);
            return *tool_result_message;
        }

        nlohmann::json execute_native(
            const ToolCall& call,
            const ToolSpec& spec,
            const std::filesystem::path& workspace,
            bool refresh,
            std::uint32_t timeout_ms)
        {
            if (call.name != "edit_file")
            {
                return error_result(
                    call.id,
                    call.name,
                    "unsupported_native_tool",
                    "Native tool is not implemented");
            }

            nlohmann::json requests = nlohmann::json::array();
            if (call.arguments.is_array())
            {
                for (const nlohmann::json& item : call.arguments)
                {
                    if (!item.is_object())
                    {
                        return error_result(
                            call.id,
                            call.name,
                            "invalid_arguments",
                            "Tool arguments array must contain only objects");
                    }
                    requests.push_back(item);
                }
            }
            else
            {
                requests.push_back(call.arguments);
            }

            if (requests.empty())
            {
                return error_result(
                    call.id,
                    call.name,
                    "invalid_arguments",
                    "Tool arguments must not be empty");
            }

            const nlohmann::json envelope = {
                {"version", 1},
                {"tool", call.name},
                {"call_id", call.id},
                {"arguments", {
                    {"requests", std::move(requests)}
                }}
            };

            const std::filesystem::path executable = edit_file_path();
            sandbox::process_request process;
            process.executable = executable;
            process.arguments = {"--toolcall-stdin"};
            process.working_directory = workspace;
            process.stdin_data = envelope.dump();
            process.timeout = std::chrono::milliseconds(timeout_ms);
            process.network = spec.network;
            process.refresh = refresh;

            if (spec.filesystem.has_value())
                add_filesystem(process.filesystem, workspace, *spec.filesystem);
            add_filesystem(
                process.filesystem,
                executable,
                sandbox::permission::read_only);

            const sandbox::process_result result = sandbox::process(process);
            if (const auto failure = process_failure(call, result))
                return *failure;

            const nlohmann::json payload = nlohmann::json::parse(
                result.stdout_text,
                nullptr,
                false);
            if (payload.is_discarded())
            {
                return error_result(
                    call.id,
                    call.name,
                    "invalid_tool_output",
                    "Native tool returned invalid JSON",
                    {
                        {"exit_code", result.state.exit_code},
                        {"stderr", result.stderr_text}
                    });
            }

            return tool_result(call.id, payload);
        }
    }

    nlohmann::json execute_tool(
        const nlohmann::json& tool_call,
        const std::filesystem::path& workspace_path,
        bool refresh,
        std::uint32_t timeout_ms)
    {
        if (timeout_ms == 0)
        {
            throw std::invalid_argument(
                "tool_runtime: timeout_ms must be positive");
        }

        const ToolCall call = parse_tool_call(tool_call);
        const auto spec = tool_specs().find(call.name);
        if (spec == tool_specs().end())
        {
            return error_result(
                call.id,
                call.name,
                "unsupported_tool",
                "Tool is not registered: " + call.name);
        }

        const std::filesystem::path workspace =
            canonical_existing_directory(workspace_path);

        if (spec->second.runtime == Runtime::typescript)
        {
            return execute_typescript(
                call,
                spec->second,
                workspace,
                refresh,
                timeout_ms);
        }

        return execute_native(
            call,
            spec->second,
            workspace,
            refresh,
            timeout_ms);
    }
}
