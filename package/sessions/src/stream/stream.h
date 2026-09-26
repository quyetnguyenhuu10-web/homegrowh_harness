#pragma once

#include <event_port>

#include <functional>
#include <string_view>

namespace sessions
{
    enum class StreamType
    {
        reasoning,
        content,
        summary_start,
        summary_reasoning,
        summary_content,
        summary_end,
        http_error,
        secondary_error,
        tool_call,
        tool_result,
        context_usage,
    };

    using StreamCallback =
        std::function<void(StreamType type, std::string_view delta)>;

    using EventLogCallback =
        std::function<void(const event_port::Event& event)>;
}
