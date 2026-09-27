#pragma once

#include "session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

namespace sessions
{
    namespace detail
    {
        struct PreparedToolCall;
    }

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
        [[nodiscard]] const nlohmann::json& canonical_call() const;

    private:
        ToolStage(
            std::uint64_t generation,
            std::uint64_t token,
            std::size_t index,
            std::unique_ptr<detail::PreparedToolCall>&& prepared) noexcept;

        std::uint64_t generation_ = 0;
        std::uint64_t token_ = 0;
        std::size_t index_ = 0;
        std::unique_ptr<detail::PreparedToolCall> prepared_;

        friend ToolStage declare_tool(Session& session);
        friend void run_tool(Session& session, ToolStage&& stage);
    };

    ToolStage declare_tool(Session& session);
    void run_tool(Session& session, ToolStage&& stage);
}
