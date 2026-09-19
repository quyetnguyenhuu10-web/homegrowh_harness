#pragma once

// Coordination probes for the Interference_rejection_rate fork.
//
// The forked edit pipeline calls these hooks synchronously on the edit
// thread at exact pipeline points, so the rejection test can interfere
// deterministically (lock / modify / delete / rename) instead of guessing
// request timing from directory-change events.
//
// Rules for hooks:
// - They run on the edit thread. Keep them short and never throw.
// - Use ev.wait_observed(timeout_ms) after mutating the file so the fork
//   watcher has observed the change before the pipeline continues.
//   Without this wait the commit gate may run before the watcher thread
//   processes the event and the interference can be missed.

#include <filesystem>
#include <functional>

namespace fsystem::windows_irr
{
    enum class IrrPoint
    {
        WatcherReady, // Fork watcher constructed and ready, before request 0.
        RequestStart, // Before source.open. Ideal point for lock interference.
        SourceOpened, // After source.open succeeded.
        ScanDone,     // Match finished with note == none, before temp build.
        TempReady,    // Temp file ready, just before commit_replace.
                      // Ideal point for modify / delete / rename.
        AfterCommit,  // After commit_replace returned.
        RequestEnd,   // Before edit_one returns (all exits).
    };

    inline const char* irr_point_name(IrrPoint point) noexcept
    {
        switch (point)
        {
            case IrrPoint::WatcherReady:
                return "watcher_ready";

            case IrrPoint::RequestStart:
                return "request_start";

            case IrrPoint::SourceOpened:
                return "source_opened";

            case IrrPoint::ScanDone:
                return "scan_done";

            case IrrPoint::TempReady:
                return "temp_ready";

            case IrrPoint::AfterCommit:
                return "after_commit";

            case IrrPoint::RequestEnd:
                return "request_end";
        }

        return "unknown";
    }

    struct IrrEvent
    {
        IrrPoint point = IrrPoint::RequestStart;
        int request_index = -1;
        std::filesystem::path path;
        // Source temp path. Only set for TempReady / AfterCommit.
        std::filesystem::path temp_path;
        // Poll the fork watcher until it observes an interference.
        // Returns true when file_changed / cancel / timeout is visible.
        // Empty for WatcherReady (nothing to observe yet).
        std::function<bool(int timeout_ms)> wait_observed;
    };

    using IrrHook = std::function<void(const IrrEvent&)>;

    struct IrrProbes
    {
        IrrHook hook; // Null = no coordination, behaves like upstream.

        // When true, the fork closes the observation window before the
        // request starts: the watcher is still constructed (same setup
        // path) but stopped immediately, so it can never report a
        // change. The pipeline itself (edit_one) is untouched. Used to
        // measure raw edit throughput without observation.
        bool disable_watcher = false;
    };
}
