#pragma once

#include <cstdint>
#include <filesystem>

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
    };

    WatcherResult watcher(
        const std::filesystem::path& path,
        int timeout_f
    );
}
