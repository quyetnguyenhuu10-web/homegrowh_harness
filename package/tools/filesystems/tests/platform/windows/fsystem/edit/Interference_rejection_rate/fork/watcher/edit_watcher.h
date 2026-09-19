#pragma once

#include "windows_watcher.h"

#include <cstdint>
#include <atomic>
#include <exception>
#include <filesystem>
#include <limits>
#include <span>
#include <thread>

namespace fsystem::windows_irr
{
    inline constexpr int edit_watcher_timeout =
        std::numeric_limits<int>::max();

    inline constexpr std::uint32_t watcher_thread_exception_error =
        std::numeric_limits<std::uint32_t>::max();

    inline constexpr std::uint32_t watcher_stop_timeout_ms = 10000;

    /*
     * Internal watcher owner used by edit.  It keeps the native state and
     * the thread lifecycle out of fsystem/watcher/watcher.h.
     */
    class edit_watcher
    {
    public:
        explicit edit_watcher(
            std::span<const std::filesystem::path> paths
        );

        edit_watcher(const edit_watcher&) = delete;
        edit_watcher& operator=(const edit_watcher&) = delete;

        ~edit_watcher() noexcept;

        void stop() noexcept;

        bool finished_before_ready() const noexcept;
        bool cancellation_requested() const noexcept;
        bool file_changed() const noexcept;
        bool timeout_requested() const noexcept;
        bool cleanup_timed_out() const noexcept;
        bool timed_out() const noexcept;
        bool has_exception() const noexcept;

        const WatcherResult& result() const noexcept;
        std::exception_ptr exception() const noexcept;

        watcher_common::WatcherState& state() noexcept;
        const watcher_common::WatcherState& state() const noexcept;

    private:
        watcher_common::WatcherState state_;
        WatcherResult result_{};
        std::exception_ptr exception_;
        std::thread thread_;
        std::atomic_bool stop_started_{false};
    };
}
