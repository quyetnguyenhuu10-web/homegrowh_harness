#pragma once

#include <sandbox>

#include <cstdint>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace sessions::detail
{
    struct HandledToolCall
    {
        nlohmann::json tool_call;
        nlohmann::json result_message;
    };

    class ToolCallHandler final
    {
    public:
        ToolCallHandler(
            const std::filesystem::path& workspace_path,
            const std::filesystem::path& tool_runtime_executable,
            const sandbox::config& sandbox_config,
            std::uint32_t tool_result_timeout_ms,
            bool refresh_workspace);

        HandledToolCall execute(nlohmann::json raw_tool_call);
        HandledToolCall handle(const nlohmann::json& raw_tool_call);

    private:
        HandledToolCall finish(HandledToolCall handled) const;

        const std::filesystem::path& workspace_path_;
        const std::filesystem::path& tool_runtime_executable_;
        const sandbox::config& sandbox_config_;
        std::uint32_t tool_result_timeout_ms_ = 0;
        bool refresh_pending_ = false;
    };
}
