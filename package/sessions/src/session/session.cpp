#include "session.h"
#include "session_internal.h"

#include <context/context.h>
#include <session/session_state.h>

#include <context_usage>
#include <secrets>

#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>

namespace sessions::detail
{
    namespace
    {
        const char* state_name(SessionState state) noexcept
        {
            switch (state)
            {
                case SessionState::request: return "request";
                case SessionState::response: return "response";
                case SessionState::tool: return "tool";
                case SessionState::finished: return "finished";
                case SessionState::closed: return "closed";
            }
            return "unknown";
        }

        std::uint32_t normalize_timeout_ms(
            int timeout,
            std::string_view name)
        {
            if (timeout == -1)
                return (std::numeric_limits<std::uint32_t>::max)();

            if (timeout <= 0)
            {
                throw std::invalid_argument(
                    std::string(name) + " must be -1 or a positive integer");
            }

            return static_cast<std::uint32_t>(timeout);
        }

        void emit_secondary_cleanup_error(SessionData& data) noexcept
        {
            const CredentialSecondaryCleanupError* secondary =
                data.credential.secondary_cleanup_error();
            if (secondary == nullptr || !data.stream)
                return;

            try
            {
                nlohmann::json payload = {
                    {"source", "credential_cleanup"}
                };

                if (secondary->result.has_value())
                {
                    payload["code"] = secondary->result->error.code;
                    payload["operation"] = secondary->result->error.operation;
                }
                else if (secondary->exception != nullptr)
                {
                    try
                    {
                        std::rethrow_exception(secondary->exception);
                    }
                    catch (const std::exception& error)
                    {
                        payload["exception"] = error.what();
                    }
                    catch (...)
                    {
                        payload["exception"] = "unknown exception";
                    }
                }

                const std::string serialized = payload.dump();
                data.stream(StreamType::secondary_error, serialized);
            }
            catch (...)
            {
            }
        }
    }

    void require_state(SessionData& data, SessionState expected)
    {
        if (data.state != expected)
        {
            throw std::logic_error(
                std::string("sessions stage mismatch: expected ") +
                state_name(expected) +
                ", current " +
                state_name(data.state));
        }
    }

    std::uint64_t declare_stage(SessionData& data)
    {
        const std::uint64_t token = data.next_stage_token++;
        data.active_stage_token = token;
        return token;
    }

    void require_stage(
        SessionData& data,
        SessionState expected,
        std::uint64_t generation,
        std::uint64_t token)
    {
        require_state(data, expected);
        if (generation != data.generation || token != data.active_stage_token)
            throw std::logic_error("sessions stage is stale");
    }

    void commit_stage(SessionData& data, SessionState next_state) noexcept
    {
        data.active_stage_token = 0;
        ++data.generation;
        data.state = next_state;
    }

    void fail_session(SessionData& data) noexcept
    {
        if (data.state == SessionState::closed)
            return;

        data.credential.close_after_primary_error();
        emit_secondary_cleanup_error(data);
        data.session_timeout.cancel();
        data.active_stage_token = 0;
        ++data.generation;
        data.state = SessionState::closed;
    }
}

namespace sessions
{
    Session::Session(std::unique_ptr<detail::SessionData>&& data) noexcept
        : data_(std::move(data))
    {
    }

    Session::Session(Session&&) noexcept = default;
    Session& Session::operator=(Session&&) noexcept = default;
    Session::~Session() = default;

    SessionState Session::state() const noexcept
    {
        return data_ == nullptr ? SessionState::closed : data_->state;
    }

    bool Session::finished() const noexcept
    {
        return state() == SessionState::finished;
    }

    Session register_session(SessionConfig&& config)
    {
        if (!config.history.is_array())
            throw std::invalid_argument("history must be a message array");
        if (!config.tool_definitions.is_array())
            throw std::invalid_argument("tool_definitions must be an array");
        if (config.model_id.empty())
            throw std::invalid_argument("model_id must not be empty");
        if (config.endpoint.empty())
            throw std::invalid_argument("endpoint must not be empty");
        if (config.context_limit == 0)
            throw std::invalid_argument("context_limit must be positive");
        if (config.api_key_raw.empty())
            throw std::invalid_argument("api_key_raw must not be empty");

        const std::uint32_t tool_result_timeout_ms = normalize_timeout_ms(
            config.tool_result_timeout_ms,
            "tool_result_timeout_ms");
        const std::uint32_t session_timeout_ms = normalize_timeout_ms(
            config.session_timeout_ms,
            "session_timeout_ms");

        (void)detail::current_messages(config.session_current);

        detail::CredentialOwner credential =
            detail::persist_session_credential(config.api_key_raw);

        const std::uint64_t tools_estimate =
            detail::tool_definition_estimate(config.tool_definitions);
        const std::uint64_t usage_checkpoint = detail::checked_add(
            context_usage::estimate(config.history),
            tools_estimate);

        return Session(std::make_unique<detail::SessionData>(
            std::move(config.history),
            std::move(config.session_current),
            std::move(config.tool_definitions),
            config.provider,
            std::move(config.endpoint),
            std::move(config.model_id),
            config.context_limit,
            config.compact_threshold,
            tool_result_timeout_ms,
            session_timeout_ms,
            std::move(config.compaction_prompt),
            std::move(config.workspace_path),
            std::move(config.stream),
            std::move(config.event_log),
            std::move(credential),
            tools_estimate,
            usage_checkpoint,
            config.refresh_workspace));
    }

    SessionResult close_session(Session&& session)
    {
        if (session.data_ == nullptr)
            throw std::logic_error("session is already closed");

        detail::SessionData& data = *session.data_;
        if (data.state == SessionState::closed)
            throw std::logic_error("session is already closed");

        const secrets::SecretOperationResult cleanup = data.credential.close();
        if (cleanup.status == secrets::SecretStatus::failed)
        {
            const std::string operation = cleanup.error.operation.empty()
                ? "erase session credential"
                : cleanup.error.operation;
            if (cleanup.error.code != 0)
            {
                throw std::system_error(
                    static_cast<int>(cleanup.error.code),
                    std::system_category(),
                    operation);
            }
            throw std::runtime_error(operation + " failed");
        }

        data.session_timeout.cancel();
        data.state = SessionState::closed;
        data.active_stage_token = 0;
        ++data.generation;

        SessionResult result{
            std::move(data.history),
            std::move(data.last_usage)
        };
        session.data_.reset();
        return result;
    }
}
