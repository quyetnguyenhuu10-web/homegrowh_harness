#include <event_port>

#include "detail/event_access.h"

#include <utility>

namespace event_port
{
    Reference::Reference(std::string&& type, std::string&& value) noexcept
        : type(std::move(type)),
          value(std::move(value))
    {
    }

    Event::Event(
        std::uint64_t sequence,
        std::chrono::system_clock::time_point timestamp,
        std::string&& package,
        Level level,
        std::string&& type,
        References&& references,
        nlohmann::json&& data)
        : sequence(sequence),
          timestamp(timestamp),
          package(std::move(package)),
          level(level),
          type(std::move(type)),
          references(std::move(references)),
          data(std::move(data))
    {
    }

    EventPtr detail::EventAccess::make(
        std::uint64_t sequence,
        std::chrono::system_clock::time_point timestamp,
        std::string&& package,
        Level level,
        std::string&& type,
        References&& references,
        nlohmann::json&& data)
    {
        return EventPtr(new Event(
            sequence,
            timestamp,
            std::move(package),
            level,
            std::move(type),
            std::move(references),
            std::move(data)));
    }
}
