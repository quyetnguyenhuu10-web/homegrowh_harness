#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

namespace tool_runtime
{
    struct process_plan
    {
        bool process_required = true;
        std::filesystem::path executable;
        std::vector<std::string> arguments;
        std::filesystem::path working_directory;
        std::string stdin_data;
        nlohmann::json immediate_result;
    };

    struct process_result_view
    {
        bool started = false;
        int exit_code = -1;
        std::error_code final_error;
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
    process_plan dispatch_tool(const nlohmann::json& input);
    normalized_result normalize_tool(
        const nlohmann::json& input,
        const process_result_view& result);

    execution_result execute_tool(const nlohmann::json& input);
    execution_result execute_invalid_json(std::string_view raw);
}
