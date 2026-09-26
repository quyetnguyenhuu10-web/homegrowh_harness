#include "request.h"
#include "session_internal.h"

#include <context/context.h>

#include <context_usage>

#include <utility>

namespace sessions
{
    RequestStage::RequestStage(
        std::uint64_t generation,
        std::uint64_t token,
        bool compact,
        std::uint64_t context_usage,
        std::uint64_t context_limit,
        std::string model,
        provider::Provider selected_provider) noexcept
        : generation_(generation),
          token_(token),
          compact_(compact),
          context_usage_(context_usage),
          context_limit_(context_limit),
          model_(std::move(model)),
          selected_provider_(selected_provider)
    {
    }

    bool RequestStage::compact() const noexcept
    {
        return compact_;
    }

    std::uint64_t RequestStage::context_usage() const noexcept
    {
        return context_usage_;
    }

    std::uint64_t RequestStage::context_limit() const noexcept
    {
        return context_limit_;
    }

    const std::string& RequestStage::model() const noexcept
    {
        return model_;
    }

    provider::Provider RequestStage::selected_provider() const noexcept
    {
        return selected_provider_;
    }

    RequestStage declare_request(Session& session)
    {
        detail::SessionData& data = *session.data_;
        detail::require_state(data, SessionState::request);

        try
        {
            const std::uint64_t current_estimate =
                context_usage::estimate(data.session_current);
            const std::uint64_t total_usage = detail::checked_add(
                data.usage_checkpoint,
                current_estimate);
            const bool compact = detail::should_compact(
                data.usage_checkpoint,
                current_estimate,
                data.compact_threshold);
            const std::uint64_t token = detail::declare_stage(data);

            return RequestStage(
                data.generation,
                token,
                compact,
                total_usage,
                data.context_limit,
                data.model_id,
                data.selected_provider);
        }
        catch (...)
        {
            detail::fail_session(data);
            throw;
        }
    }

    void run_request(Session& session, RequestStage&& stage)
    {
        detail::SessionData& data = *session.data_;
        detail::require_stage(
            data,
            SessionState::request,
            stage.generation_,
            stage.token_);

        try
        {
            data.pending_turn = detail::run_turn(
                data.credential.signature(),
                data.endpoint,
                data.model_id,
                data.selected_provider,
                data.compaction_prompt,
                data.session_current,
                data.tool_definitions,
                stage.compact_,
                data.history,
                data.stream,
                data.event_log);

            detail::commit_stage(data, SessionState::response);
        }
        catch (...)
        {
            detail::fail_session(data);
            throw;
        }
    }
}
