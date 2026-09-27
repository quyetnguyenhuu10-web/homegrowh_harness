#include "response.h"
#include "session_internal.h"

#include <context/context.h>
#include <session/session_state.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace sessions
{
    ResponseStage::ResponseStage(
        std::uint64_t generation,
        std::uint64_t token,
        bool has_tool_calls,
        std::size_t tool_count,
        provider::RequestUsage usage)
        : generation_(generation),
          token_(token),
          has_tool_calls_(has_tool_calls),
          tool_count_(tool_count),
          usage_(std::move(usage))
    {
    }

    bool ResponseStage::has_tool_calls() const noexcept
    {
        return has_tool_calls_;
    }

    std::size_t ResponseStage::tool_count() const noexcept
    {
        return tool_count_;
    }

    const provider::RequestUsage& ResponseStage::usage() const noexcept
    {
        return usage_;
    }

    ResponseStage declare_response(Session& session)
    {
        detail::SessionData& data = *session.data_;
        detail::require_state(data, SessionState::response);
        if (!data.pending_turn.has_value())
            throw std::logic_error("sessions response has no pending request result");

        const nlohmann::json& assistant = data.pending_turn->assistant;
        const bool has_tools = detail::has_tool_calls(assistant);
        const std::size_t tool_count = has_tools
            ? assistant.at("tool_calls").size()
            : 0;
        const std::uint64_t token = detail::declare_stage(data);

        return ResponseStage(
            data.generation,
            token,
            has_tools,
            tool_count,
            data.pending_turn->request.usage);
    }

    void run_response(Session& session, ResponseStage&& stage)
    {
        detail::SessionData& data = *session.data_;
        detail::require_stage(
            data,
            SessionState::response,
            stage.generation_,
            stage.token_);
        if (!data.pending_turn.has_value())
            throw std::logic_error("sessions response has no pending request result");

        try
        {
            detail::TurnResult turn = std::move(*data.pending_turn);
            data.pending_turn.reset();

            data.history = std::move(turn.request.messages);
            data.history.push_back(std::move(turn.assistant));
            data.last_usage = std::move(turn.request.usage);
            data.usage_checkpoint = detail::exact_context_usage(data.last_usage);

            if (data.stream)
            {
                const nlohmann::json payload = {
                    {"used", data.usage_checkpoint},
                    {"limit", data.context_limit}
                };
                const std::string serialized = payload.dump();
                data.stream(StreamType::context_usage, serialized);
            }

            if (!stage.has_tool_calls_)
            {
                detail::commit_stage(data, SessionState::finished);
                return;
            }

            data.tool_index = 0;
            data.canonical_tool_calls = nlohmann::json::array();
            data.tool_results = nlohmann::json::array();
            detail::commit_stage(data, SessionState::tool);
        }
        catch (...)
        {
            detail::fail_session(data, std::current_exception());
            if (data.failure.has_value())
                detail::emit_session_failure(*data.failure, data.event_log);
            throw;
        }
    }
}
