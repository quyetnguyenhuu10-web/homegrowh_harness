#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>

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
        using stop_notifier = void (*)(void*) noexcept;

        std::atomic_bool stop_requested{false};
        std::atomic_bool file_changed{false};
        std::atomic_bool ready{false};
        std::atomic_bool finished{false};

        /*
         * Platform implementations may register a native wake-up target
         * here.  request_watcher_stop() invokes it while holding the mutex,
         * which keeps the target alive until the notification is posted.
         */
        std::mutex stop_signal_mutex;
        stop_notifier stop_signal = nullptr;
        void* stop_signal_context = nullptr;
    };

    void request_watcher_stop(WatcherState& state) noexcept;

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
