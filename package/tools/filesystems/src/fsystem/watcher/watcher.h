#pragma once

#include <atomic>
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

    /*
     * Shared state used when edit runs the watcher in a background thread.
     * The watcher only publishes whether the watched file changed; it never
     * publishes or copies raw event records.
     */
    struct WatcherState
    {
        std::atomic_bool stop_requested{false};
        std::atomic_bool file_changed{false};
        std::atomic_bool ready{false};
        std::atomic_bool finished{false};
    };

    WatcherResult watcher(
        std::filesystem::path path,
        int timeout_f
    );

    WatcherResult watcher(
        std::filesystem::path path,
        int timeout_f,
        WatcherState& state
    );
}
