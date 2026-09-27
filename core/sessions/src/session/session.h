#pragma once

#include <stream/stream.h>

#include <cstdint>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>
#include <provider>

namespace sessions
{
    namespace detail
    {
        struct SessionData;
    }

    class RequestStage;
    class ResponseStage;
    class ToolStage;

    enum class SessionState
    {
        request,
        response,
        tool,
        finished,
        closed,
    };

    struct SessionConfig
    {
        std::string api_key_raw;
        nlohmann::json history = nlohmann::json::array();
        nlohmann::json session_current;
        nlohmann::json tool_definitions = nlohmann::json::array();
        provider::Provider provider = provider::Provider::openai;
        std::string endpoint;
        std::string model_id;
        std::uint64_t context_limit = 0;
        std::uint64_t compact_threshold = 0;
        int session_timeout_ms = -1;
        std::string compaction_prompt;
        std::string tool_body;
        StreamCallback stream;
        EventLogCallback event_log;
    };

    struct SessionResult
    {
        nlohmann::json history;
        provider::RequestUsage usage = provider::UsageState::unavailable;
    };

    class Session final
    {
    public:
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        Session(Session&&) noexcept;
        Session& operator=(Session&&) noexcept;
        ~Session();

        [[nodiscard]] SessionState state() const noexcept;
        [[nodiscard]] bool finished() const noexcept;

    private:
        explicit Session(std::unique_ptr<detail::SessionData>&& data) noexcept;

        std::unique_ptr<detail::SessionData> data_;

        friend Session register_session(SessionConfig&& config);
        friend SessionResult close_session(Session&& session);
        friend RequestStage declare_request(Session& session);
        friend void run_request(Session& session, RequestStage&& stage);
        friend ResponseStage declare_response(Session& session);
        friend void run_response(Session& session, ResponseStage&& stage);
        friend ToolStage declare_tool(Session& session);
        friend void run_tool(Session& session, ToolStage&& stage);
    };

    Session register_session(SessionConfig&& config);
    SessionResult close_session(Session&& session);
}
