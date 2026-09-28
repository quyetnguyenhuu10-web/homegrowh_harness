#include "tool.h"
#include "session_internal.h"

#include <session/session_state.h>
#include <tool/tool_call.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace sessions
{
    namespace
    {
        std::string_view string_field(
            const nlohmann::json& object,
            std::string_view key)
        {
            static constexpr std::string_view empty;
            if (!object.is_object())
                return empty;

            const auto value = object.find(std::string(key));
            if (value == object.end() || !value->is_string())
                return empty;

            return value->get_ref<const std::string&>();
        }
    }

    ToolStage::ToolStage(
        std::uint64_t generation,
        std::uint64_t token,
        std::size_t index,
        nlohmann::json raw_tool_call) noexcept
        : generation_(generation),
          token_(token),
          index_(index),
          raw_tool_call_(std::move(raw_tool_call))
    {
    }

    ToolStage::ToolStage(ToolStage&&) noexcept = default;
    ToolStage& ToolStage::operator=(ToolStage&&) noexcept = default;
    ToolStage::~ToolStage() = default;

    std::string_view ToolStage::call_id() const
    {
        return string_field(raw_tool_call_, "id");
    }

    std::string_view ToolStage::name() const
    {
        if (!raw_tool_call_.is_object())
            return {};

        const auto function = raw_tool_call_.find("function");
        if (function == raw_tool_call_.end() || !function->is_object())
            return {};

        return string_field(*function, "name");
    }

    std::string_view ToolStage::arguments() const
    {
        if (!raw_tool_call_.is_object())
            return {};

        const auto function = raw_tool_call_.find("function");
        if (function == raw_tool_call_.end() || !function->is_object())
            return {};

        return string_field(*function, "arguments");
    }

    const nlohmann::json& ToolStage::raw_call() const noexcept
    {
        return raw_tool_call_;
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

            const std::uint64_t token = detail::declare_stage(data);

            return ToolStage(
                data.generation,
                token,
                data.tool_index,
                calls.at(data.tool_index));
        }
        catch (...)
        {
            detail::fail_session(data, std::current_exception());
            if (data.failure.has_value())
                detail::emit_session_failure(*data.failure);
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
        if (stage.index_ != data.tool_index)
            throw std::logic_error("sessions tool stage is stale");

        try
        {
            detail::HandledToolCall handled =
                data.tool_handler.execute(
                    std::move(stage.raw_tool_call_));

            data.runtime_tool_calls.push_back(std::move(handled.tool_call));
            data.tool_results.push_back(std::move(handled.result_message));
            ++data.tool_index;

            nlohmann::json& assistant = data.history.back();
            const std::size_t total_tools = assistant.at("tool_calls").size();
            if (data.tool_index < total_tools)
            {
                detail::commit_stage(data, SessionState::tool);
                return;
            }

            assistant["tool_calls"] = std::move(data.runtime_tool_calls);
            data.session_current = {
                {"messages", std::move(data.tool_results)}
            };
            detail::commit_stage(data, SessionState::request);
        }
        catch (...)
        {
            detail::fail_session(data, std::current_exception());
            if (data.failure.has_value())
                detail::emit_session_failure(*data.failure);
            throw;
        }
    }
}
