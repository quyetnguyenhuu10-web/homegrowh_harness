#include "tool.h"
#include "session_internal.h"

#include <session/session_state.h>
#include <tool/tool_call.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace sessions
{
    ToolStage::ToolStage(
        std::uint64_t generation,
        std::uint64_t token,
        std::size_t index,
        std::unique_ptr<detail::PreparedToolCall>&& prepared) noexcept
        : generation_(generation),
          token_(token),
          index_(index),
          prepared_(std::move(prepared))
    {
    }

    ToolStage::ToolStage(ToolStage&&) noexcept = default;
    ToolStage& ToolStage::operator=(ToolStage&&) noexcept = default;
    ToolStage::~ToolStage() = default;

    std::string_view ToolStage::call_id() const
    {
        return prepared_->tool_call.at("id").get_ref<const std::string&>();
    }

    std::string_view ToolStage::name() const
    {
        return prepared_->tool_call.at("function").at("name")
            .get_ref<const std::string&>();
    }

    std::string_view ToolStage::arguments() const
    {
        return prepared_->tool_call.at("function").at("arguments")
            .get_ref<const std::string&>();
    }

    const nlohmann::json& ToolStage::canonical_call() const
    {
        return prepared_->tool_call;
    }

    ToolStage declare_tool(Session& session)
    {
        detail::SessionData& data = *session.data_;
        detail::require_state(data, SessionState::tool);

        try
        {
            nlohmann::json& assistant = data.history.back();
            if (!detail::has_tool_calls(assistant))
                throw std::logic_error("sessions tool state has no tool calls");

            const nlohmann::json& calls = assistant.at("tool_calls");
            if (data.tool_index >= calls.size())
                throw std::logic_error("sessions tool index is out of range");

            auto prepared = std::make_unique<detail::PreparedToolCall>(
                data.tool_handler.prepare(calls.at(data.tool_index)));
            const std::uint64_t token = detail::declare_stage(data);

            return ToolStage(
                data.generation,
                token,
                data.tool_index,
                std::move(prepared));
        }
        catch (...)
        {
            detail::fail_session(data);
            throw;
        }
    }

    void run_tool(Session& session, ToolStage&& stage)
    {
        detail::SessionData& data = *session.data_;
        detail::require_stage(
            data,
            SessionState::tool,
            stage.generation_,
            stage.token_);
        if (stage.index_ != data.tool_index || stage.prepared_ == nullptr)
            throw std::logic_error("sessions tool stage is stale");

        try
        {
            detail::HandledToolCall handled =
                data.tool_handler.execute(std::move(*stage.prepared_));

            data.canonical_tool_calls.push_back(std::move(handled.tool_call));
            data.tool_results.push_back(std::move(handled.result_message));
            ++data.tool_index;

            nlohmann::json& assistant = data.history.back();
            const std::size_t total_tools = assistant.at("tool_calls").size();
            if (data.tool_index < total_tools)
            {
                detail::commit_stage(data, SessionState::tool);
                return;
            }

            assistant["tool_calls"] = std::move(data.canonical_tool_calls);
            data.session_current = {
                {"messages", std::move(data.tool_results)}
            };
            detail::commit_stage(data, SessionState::request);
        }
        catch (...)
        {
            detail::fail_session(data);
            throw;
        }
    }
}
