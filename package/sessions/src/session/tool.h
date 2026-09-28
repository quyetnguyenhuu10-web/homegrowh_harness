#pragma once

#include "session.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <nlohmann/json.hpp>

namespace sessions
{
    class ToolStage final
    {
    public:
        ToolStage(const ToolStage&) = delete;
        ToolStage& operator=(const ToolStage&) = delete;
        ToolStage(ToolStage&&) noexcept;
        ToolStage& operator=(ToolStage&&) noexcept;
        ~ToolStage();

        [[nodiscard]] std::string_view call_id() const;
        [[nodiscard]] std::string_view name() const;
        [[nodiscard]] std::string_view arguments() const;
        [[nodiscard]] const nlohmann::json& raw_call() const noexcept;

    private:
        ToolStage(
            std::uint64_t generation,
            std::uint64_t token,
            std::size_t index,
            nlohmann::json raw_tool_call) noexcept;

        std::uint64_t generation_ = 0;
        std::uint64_t token_ = 0;
        std::size_t index_ = 0;
        nlohmann::json raw_tool_call_;

        friend ToolStage declare_tool(Session& session);
        friend void run_tool(Session& session, ToolStage&& stage);
    };

    ToolStage declare_tool(Session& session);
    void run_tool(Session& session, ToolStage&& stage);
}
