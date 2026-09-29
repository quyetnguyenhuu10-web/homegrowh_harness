#pragma once

#include "../../watcher/windows_watcher.h"

#include <atomic>

namespace fsystem::windows_irr::detail
{
    using watcher_state =
        fsystem::windows_irr::watcher_common::WatcherState;

    inline bool cancellation_requested(
        const watcher_state& state
    ) noexcept
    {
        return state.cancel_requested.load(
            std::memory_order_acquire
        );
    }

    inline bool timeout_requested(
        const watcher_state& state
    ) noexcept
    {
        return state.timeout_requested.load(
            std::memory_order_acquire
        );
    }

    inline bool begin_commit(watcher_state& state) noexcept
    {
        return fsystem::windows_irr::watcher_common::begin_edit_commit(
            state
        );
    }

    inline void mark_committed(watcher_state& state) noexcept
    {
        fsystem::windows_irr::watcher_common::mark_edit_committed(
            state
        );
    }

    inline void prepare_next_edit(watcher_state& state) noexcept
    {
        fsystem::windows_irr::watcher_common::prepare_next_edit(state);
    }
}
