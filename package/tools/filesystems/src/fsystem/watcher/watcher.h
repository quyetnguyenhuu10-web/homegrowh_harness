#pragma once

#include <filesystem>
#include <string>
#include <cstdint>
#include <cstddef>
#include <vector>

namespace fsystem
{
    enum class EventStatus
    {
        None,
        HasEvent,
        NoEvent
    };

    struct WatcherResult
    {
        EventStatus event_status = EventStatus::None;
        std::uint32_t error = 0;
        std::vector<std::vector<std::byte>> events;
        std::vector<std::vector<std::byte>> file_events;
    };

    WatcherResult watcher(
        std::filesystem::path path,
        int timeout_f
    );
}
