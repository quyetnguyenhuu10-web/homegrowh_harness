#pragma once

#include <event_port>

namespace event_port::detail
{
    struct EventAccess
    {
        static Result<EventPtr> make(
            std::uint64_t sequence,
            std::chrono::system_clock::time_point timestamp,
            std::string&& package,
            Level level,
            std::string&& type,
            References&& references,
            nlohmann::json&& data);
    };
}
