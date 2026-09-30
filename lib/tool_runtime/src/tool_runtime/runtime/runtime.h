#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>
#include <tool_runtime/tool_runtime.h>

namespace tool_runtime
{
    struct process_plan
    {
        std::filesystem::path executable;
        std::vector<std::string> arguments;
        std::filesystem::path working_directory;
        std::string stdin_data;
    };

    struct process_result_view
    {
        bool started = false;
        std::int64_t exit_code = -1;
        std::optional<Error> error;
        std::string stdout_text;
        std::string stderr_text;
    };

    struct normalized_result
    {
        nlohmann::json json;
        std::string stderr_text;
    };

    struct execution_result
    {
        nlohmann::json tool_call;
        nlohmann::json result;
    };
}

namespace tool_runtime::detail
{
    Result<process_plan> dispatch_tool(const nlohmann::json& input);
    Result<normalized_result> normalize_tool(
        const nlohmann::json& input,
        const process_result_view& result);

    execution_result execute_tool(const nlohmann::json& input);
    execution_result execute_error(const nlohmann::json& input, Error&& error);
    execution_result execute_invalid_json(std::string_view raw);
}
