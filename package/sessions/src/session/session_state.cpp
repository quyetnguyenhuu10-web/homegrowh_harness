#include "session_state.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

namespace sessions::detail
{
    const nlohmann::json& current_messages(
        const nlohmann::json& session_current)
    {
        if (!session_current.is_object())
            throw std::invalid_argument("session_current must be an object");
        if (session_current.contains("tools"))
        {
            throw std::invalid_argument(
                "session_current must not contain tools; pass tool_definitions separately");
        }

        const auto messages = session_current.find("messages");
        if (messages == session_current.end() || !messages->is_array())
        {
            throw std::invalid_argument(
                "session_current.messages must be an array");
        }
        if (messages->empty())
            throw std::invalid_argument("session_current.messages must not be empty");
        return *messages;
    }

    bool has_tool_calls(const nlohmann::json& assistant)
    {
        const auto calls = assistant.find("tool_calls");
        return calls != assistant.end() && calls->is_array() && !calls->empty();
    }
}
