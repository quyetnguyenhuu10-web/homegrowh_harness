#pragma once

#include <fsystem/watcher/watcher.h>

#include <atomic>

namespace fsystem::windows::detail
{
    inline bool cancellation_requested(
        const fsystem::WatcherState& watcher_state
    ) noexcept
    {
        return watcher_state.cancel_requested.load(
            std::memory_order_acquire
        );
    }
}
