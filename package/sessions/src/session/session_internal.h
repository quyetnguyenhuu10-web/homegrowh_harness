#pragma once

#include "session.h"

#include <request/turn.h>
#include <session/credential_owner.h>
#include <session/session_failure.h>
#include <session/session_timeout.h>
#include <tool/tool_call.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

namespace sessions::detail
{
    struct SessionData final
    {
        SessionData(
            nlohmann::json&& history_value,
            nlohmann::json&& session_current_value,
            nlohmann::json&& tool_definitions_value,
            provider::Provider provider_value,
            std::string&& endpoint_value,
            std::string&& model_id_value,
            std::uint64_t context_limit_value,
            std::uint64_t compact_threshold_value,
            std::uint32_t tool_result_timeout_ms_value,
            std::uint32_t session_timeout_ms_value,
            std::string&& compaction_prompt_value,
            std::filesystem::path&& workspace_path_value,
            std::filesystem::path&& tool_runtime_executable_value,
            sandbox::config&& sandbox_config_value,
            CredentialOwner&& credential_value,
            std::uint64_t tools_estimate_value,
            std::uint64_t usage_checkpoint_value,
            bool refresh_workspace)
            : history(std::move(history_value)),
              session_current(std::move(session_current_value)),
              tool_definitions(std::move(tool_definitions_value)),
              selected_provider(provider_value),
              endpoint(std::move(endpoint_value)),
              model_id(std::move(model_id_value)),
              context_limit(context_limit_value),
              compact_threshold(compact_threshold_value),
              tool_result_timeout_ms(tool_result_timeout_ms_value),
              session_timeout_ms(session_timeout_ms_value),
              compaction_prompt(std::move(compaction_prompt_value)),
              workspace_path(std::move(workspace_path_value)),
              tool_runtime_executable(std::move(tool_runtime_executable_value)),
              sandbox_config(std::move(sandbox_config_value)),
              credential(std::move(credential_value)),
              tools_estimate(tools_estimate_value),
              usage_checkpoint(usage_checkpoint_value),
              tool_handler(
                  workspace_path,
                  tool_runtime_executable,
                  sandbox_config,
                  tool_result_timeout_ms,
                  refresh_workspace),
              session_timeout(session_timeout_ms)
        {
        }

        SessionState state = SessionState::request;
        std::uint64_t generation = 0;
        std::uint64_t next_stage_token = 1;
        std::uint64_t active_stage_token = 0;

        nlohmann::json history;
        nlohmann::json session_current;
        nlohmann::json tool_definitions;
        provider::Provider selected_provider = provider::Provider::openai;
        std::string endpoint;
        std::string model_id;
        std::uint64_t context_limit = 0;
        std::uint64_t compact_threshold = 0;
        std::uint32_t tool_result_timeout_ms = 0;
        std::uint32_t session_timeout_ms = 0;
        std::string compaction_prompt;
        std::filesystem::path workspace_path;
        std::filesystem::path tool_runtime_executable;
        sandbox::config sandbox_config;
        CredentialOwner credential;
        std::uint64_t tools_estimate = 0;
        std::uint64_t usage_checkpoint = 0;
        provider::RequestUsage last_usage = provider::UsageState::unavailable;

        ToolCallHandler tool_handler;
        std::optional<TurnResult> pending_turn;

        std::size_t tool_index = 0;
        nlohmann::json runtime_tool_calls = nlohmann::json::array();
        nlohmann::json tool_results = nlohmann::json::array();
        std::optional<SessionFailure> failure;
        SessionTimeout session_timeout;
    };

    void require_state(SessionData& data, SessionState expected);
    std::uint64_t declare_stage(SessionData& data);
    void require_stage(
        SessionData& data,
        SessionState expected,
        std::uint64_t generation,
        std::uint64_t token);
    void commit_stage(SessionData& data, SessionState next_state) noexcept;
    void fail_session(
        SessionData& data,
        std::exception_ptr exception) noexcept;
}
