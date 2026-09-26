#pragma once

#include <stream/stream.h>

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

    struct PreparedToolCall
    {
        nlohmann::json tool_call;
        const nlohmann::json* definition = nullptr;
        nlohmann::json preparation_error;
        bool arguments_valid = false;
        bool synthesized_id = false;
    };

    class ToolCallHandler final
    {
    public:
        ToolCallHandler(
            const nlohmann::json& tool_definitions,
            const std::filesystem::path& workspace_path,
            std::uint32_t tool_result_timeout_ms,
            bool refresh_workspace,
            const StreamCallback* stream = nullptr);

        PreparedToolCall prepare(const nlohmann::json& raw_tool_call);
        HandledToolCall execute(PreparedToolCall&& prepared);
        HandledToolCall handle(const nlohmann::json& raw_tool_call);

    private:
        const nlohmann::json* definition(const std::string& name) const;
        std::string next_call_id();
        void emit(StreamType type, std::string_view call_id) const;
        HandledToolCall finish(
            nlohmann::json tool_call,
            nlohmann::json result_message) const;

        const nlohmann::json& tool_definitions_;
        const std::filesystem::path& workspace_path_;
        std::uint32_t tool_result_timeout_ms_ = 0;
        const StreamCallback* stream_ = nullptr;
        bool refresh_pending_ = false;
        std::uint64_t next_call_id_ = 1;
    };
}
