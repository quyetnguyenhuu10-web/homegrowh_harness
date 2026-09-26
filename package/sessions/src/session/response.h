#pragma once

#include "session.h"

#include <cstddef>
#include <cstdint>

#include <provider>

namespace sessions
{
    class ResponseStage final
    {
    public:
        ResponseStage(const ResponseStage&) = delete;
        ResponseStage& operator=(const ResponseStage&) = delete;
        ResponseStage(ResponseStage&&) noexcept = default;
        ResponseStage& operator=(ResponseStage&&) noexcept = default;

        [[nodiscard]] bool has_tool_calls() const noexcept;
        [[nodiscard]] std::size_t tool_count() const noexcept;
        [[nodiscard]] const provider::RequestUsage& usage() const noexcept;

    private:
        ResponseStage(
            std::uint64_t generation,
            std::uint64_t token,
            bool has_tool_calls,
            std::size_t tool_count,
            provider::RequestUsage usage);

        std::uint64_t generation_ = 0;
        std::uint64_t token_ = 0;
        bool has_tool_calls_ = false;
        std::size_t tool_count_ = 0;
        provider::RequestUsage usage_ = provider::UsageState::unavailable;

        friend ResponseStage declare_response(Session& session);
        friend void run_response(Session& session, ResponseStage&& stage);
    };

    ResponseStage declare_response(Session& session);
    void run_response(Session& session, ResponseStage&& stage);
}
