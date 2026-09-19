#include "windows_watcher_detail.h"

#include <atomic>

namespace fsystem::windows_irr::watcher_common::detail
{
    watcher_state_guard::watcher_state_guard(
        WatcherState* state
    ) noexcept
        : state_(state)
    {
        if (state_ == nullptr)
            return;

        state_->ready.store(
            false,
            std::memory_order_release
        );

        state_->finished.store(
            false,
            std::memory_order_release
        );

        state_->file_changed.store(
            false,
            std::memory_order_release
        );

        /*
         * A WatcherState may be reused by the caller.
         *
         * cancel_requested belongs to the current watcher lifetime,
         * so it must start in the non-cancelled state.
         */
        state_->cancel_requested.store(
            false,
            std::memory_order_release
        );

        state_->timeout_requested.store(
            false,
            std::memory_order_release
        );

        state_->cleanup_timed_out.store(
            false,
            std::memory_order_release
        );

        state_->edit_commit_phase.store(
            EditCommitPhase::Watching,
            std::memory_order_release
        );
    }

    watcher_state_guard::~watcher_state_guard() noexcept
    {
        if (state_ != nullptr)
        {
            state_->finished.store(
                true,
                std::memory_order_release
            );
        }
    }

    bool stop_requested(const WatcherState* state) noexcept
    {
        return state != nullptr &&
            state->stop_requested.load(
                std::memory_order_acquire
            );
    }

    bool publish_file_changed(WatcherState* state) noexcept
    {
        if (state == nullptr)
            return true;

        if (
            state->edit_commit_phase.load(
                std::memory_order_acquire
            ) != EditCommitPhase::Watching
        )
        {
            return false;
        }

        /*
         * file_changed is the observable watcher result.
         * cancel_requested is the active cancellation signal consumed by
         * the edit operation. Publish the result first so the edit side
         * cannot observe cancellation without the corresponding state.
         */
        state->file_changed.store(
            true,
            std::memory_order_release
        );

        state->cancel_requested.store(
            true,
            std::memory_order_release
        );

        return true;
    }
}

namespace fsystem::windows_irr::watcher_common
{
    void request_watcher_stop(WatcherState& state) noexcept
    {
        state.stop_requested.store(
            true,
            std::memory_order_release
        );

        std::lock_guard<std::mutex> lock(
            state.stop_signal_mutex
        );

        if (state.stop_signal != nullptr)
        {
            state.stop_signal(state.stop_signal_context);
        }
    }

    bool request_edit_timeout(WatcherState* state) noexcept
    {
        if (state == nullptr)
            return false;

        if (
            state->cancel_requested.load(std::memory_order_acquire) ||
            state->file_changed.load(std::memory_order_acquire)
        )
        {
            return false;
        }

        EditCommitPhase expected = EditCommitPhase::Watching;

        if (!state->edit_commit_phase.compare_exchange_strong(
                expected,
                EditCommitPhase::TimedOut,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            ))
        {
            return false;
        }

        state->timeout_requested.store(
            true,
            std::memory_order_release
        );

        state->cancel_requested.store(
            true,
            std::memory_order_release
        );

        return true;
    }

    bool begin_edit_commit(WatcherState& state) noexcept
    {
        if (
            state.timeout_requested.load(std::memory_order_acquire) ||
            state.file_changed.load(std::memory_order_acquire) ||
            state.cancel_requested.load(std::memory_order_acquire)
        )
        {
            return false;
        }

        EditCommitPhase expected = state.edit_commit_phase.load(
            std::memory_order_acquire
        );

        while (
            expected == EditCommitPhase::Watching ||
            expected == EditCommitPhase::Committed
        )
        {
            if (state.edit_commit_phase.compare_exchange_weak(
                    expected,
                    EditCommitPhase::Committing,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                ))
            {
                return true;
            }

            if (
                state.timeout_requested.load(
                    std::memory_order_acquire
                ) ||
                state.file_changed.load(
                    std::memory_order_acquire
                ) ||
                state.cancel_requested.load(
                    std::memory_order_acquire
                )
            )
            {
                return false;
            }
        }

        return false;
    }

    void mark_edit_committed(WatcherState& state) noexcept
    {
        state.edit_commit_phase.store(
            EditCommitPhase::Committed,
            std::memory_order_release
        );
    }

    void prepare_next_edit(WatcherState& state) noexcept
    {
        if (
            state.timeout_requested.load(std::memory_order_acquire) ||
            state.file_changed.load(std::memory_order_acquire) ||
            state.cancel_requested.load(std::memory_order_acquire)
        )
        {
            return;
        }

        EditCommitPhase phase = state.edit_commit_phase.load(
            std::memory_order_acquire
        );

        while (phase == EditCommitPhase::Committing)
        {
            if (state.edit_commit_phase.compare_exchange_weak(
                    phase,
                    EditCommitPhase::Watching,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                ))
            {
                return;
            }

            if (
                state.timeout_requested.load(
                    std::memory_order_acquire
                ) ||
                state.file_changed.load(
                    std::memory_order_acquire
                ) ||
                state.cancel_requested.load(
                    std::memory_order_acquire
                )
            )
            {
                return;
            }
        }
    }
}
