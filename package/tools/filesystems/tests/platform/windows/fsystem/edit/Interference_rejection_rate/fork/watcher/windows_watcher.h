#pragma once

#include "fsystem/watcher/watcher.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <span>

/*
 * Windows-native watcher logic shared by the edit and public wrappers.
 * None of these types are part of the public fsystem watcher API.
 */
namespace fsystem::windows_irr::watcher_common
{
    enum class EditCommitPhase : std::uint8_t
    {
        Watching,
        Committing,
        Committed,
        TimedOut,
    };

    struct WatcherState
    {
        using stop_notifier = void (*)(void*) noexcept;

        std::atomic_bool stop_requested{false};
        std::atomic_bool file_changed{false};
        std::atomic_bool ready{false};
        std::atomic_bool finished{false};
        std::atomic_bool cancel_requested{false};
        std::atomic_bool timeout_requested{false};
        std::atomic_bool cleanup_timed_out{false};
        std::atomic<EditCommitPhase> edit_commit_phase{
            EditCommitPhase::Watching
        };

        std::mutex stop_signal_mutex;
        stop_notifier stop_signal = nullptr;
        void* stop_signal_context = nullptr;
    };

    void request_watcher_stop(WatcherState& state) noexcept;

    bool request_edit_timeout(WatcherState* state) noexcept;
    bool begin_edit_commit(WatcherState& state) noexcept;
    void mark_edit_committed(WatcherState& state) noexcept;
    void prepare_next_edit(WatcherState& state) noexcept;

    WatcherResult watcher_files(
        std::span<const std::filesystem::path> paths,
        int timeout_f,
        WatcherState* state
    );

    WatcherResult watcher_file(
        const std::filesystem::path& path,
        int timeout_f,
        WatcherState* state
    );
}
