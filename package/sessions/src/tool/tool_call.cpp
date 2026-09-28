#include "tool_call.h"

#include <sandbox_process.h>
#include <event_port>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace sessions::detail
{
    namespace
    {
        void require_process_success(
            const sandbox::process_results& result)
        {
            if (!result.state.config.path_errors.empty())
            {
                std::ostringstream message;
                message << "sandbox config path errors:";
                for (const sandbox::config_path_error& item :
                     result.state.config.path_errors)
                {
                    message
                        << "\n  "
                        << item.path.string()
                        << " [" << item.error.value() << "] "
                        << item.error.message();
                }
                throw std::runtime_error(message.str());
            }

            if (result.state.config.final_error)
            {
                throw std::system_error(
                    result.state.config.final_error,
                    "tool runtime sandbox config failed");
            }

            if (result.state.final_error)
            {
                throw std::system_error(
                    result.state.final_error,
                    "tool runtime sandbox process failed");
            }

            if (result.state.timed_out)
            {
                if (result.state.os_error_before_termination)
                {
                    throw std::system_error(
                        result.state.os_error_before_termination,
                        "tool runtime timed out");
                }
                throw std::runtime_error("tool runtime timed out");
            }

            if (!result.state.started)
            {
                throw std::logic_error(
                    "sandbox returned started=false without an error");
            }

            if (result.state.exit_code != 0)
            {
                throw std::runtime_error(
                    "tool runtime exited with code " +
                    std::to_string(result.state.exit_code) +
                    (result.stderr_text.empty()
                        ? std::string{}
                        : "; stderr=" + result.stderr_text));
            }
        }

        HandledToolCall parse_runtime_response(
            const std::string& stdout_text)
        {
            nlohmann::json response = nlohmann::json::parse(
                stdout_text,
                nullptr,
                false);
            if (response.is_discarded() || !response.is_object())
            {
                throw std::runtime_error(
                    "tool runtime returned invalid JSON");
            }

            const auto tool_call = response.find("tool_call");
            const auto result = response.find("result");
            if (tool_call == response.end() || !tool_call->is_object())
            {
                throw std::runtime_error(
                    "tool runtime response has no tool_call object");
            }
            if (result == response.end() || !result->is_object())
            {
                throw std::runtime_error(
                    "tool runtime response has no result object");
            }

            return {
                *tool_call,
                *result
            };
        }

        std::string_view call_id_of(const nlohmann::json& tool_call)
        {
            static constexpr std::string_view empty;
            if (!tool_call.is_object())
                return empty;

            const auto id = tool_call.find("id");
            if (id == tool_call.end() || !id->is_string())
                return empty;

            return id->get_ref<const std::string&>();
        }
    }

    ToolCallHandler::ToolCallHandler(
        const std::filesystem::path& workspace_path,
        const std::filesystem::path& tool_runtime_executable,
        const sandbox::config& sandbox_config,
        std::uint32_t tool_result_timeout_ms,
        bool refresh_workspace)
        : workspace_path_(workspace_path),
          tool_runtime_executable_(tool_runtime_executable),
          sandbox_config_(sandbox_config),
          tool_result_timeout_ms_(tool_result_timeout_ms),
          refresh_pending_(refresh_workspace)
    {
    }

    HandledToolCall ToolCallHandler::finish(
        HandledToolCall handled) const
    {
        const std::string_view call_id =
            call_id_of(handled.tool_call);
        event_port::port(event_port::Emit{
            "sessions",
            event_port::Level::info,
            "tool_call",
            {},
            nlohmann::json{
                {"call_id", call_id},
                {"tool_call", handled.tool_call}
            }
        });
        event_port::port(event_port::Emit{
            "sessions",
            event_port::Level::info,
            "tool_result",
            {},
            nlohmann::json{
                {"call_id", call_id},
                {"result", handled.result_message}
            }
        });
        return handled;
    }

    HandledToolCall ToolCallHandler::execute(
        nlohmann::json raw_tool_call)
    {
        sandbox::process_request process;
        process.executable = tool_runtime_executable_;
        process.working_directory = workspace_path_;
        process.stdin_data = raw_tool_call.dump();
        process.timeout =
            std::chrono::milliseconds(tool_result_timeout_ms_);
        process.refresh = std::exchange(refresh_pending_, false);
        process.config = sandbox_config_;

        sandbox::process(process);
        require_process_success(process.results);

        return finish(parse_runtime_response(
            process.results.stdout_text));
    }

    HandledToolCall ToolCallHandler::handle(
        const nlohmann::json& raw_tool_call)
    {
        return execute(raw_tool_call);
    }
}
