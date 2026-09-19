#include "windows_watcher_detail.h"

#include <atomic>

namespace fsystem::windows::watcher_common::detail
{
    namespace
    {
        void post_cancellation_completion(void* context) noexcept
        {
            const HANDLE io_completion_port =
                static_cast<HANDLE>(context);

            if (io_completion_port == nullptr)
                return;

            (void)PostQueuedCompletionStatus(
                io_completion_port,
                0,
                cancellation_completion_key,
                nullptr
            );
        }
    }

    watcher_stop_signal_guard::watcher_stop_signal_guard(
        WatcherState* state,
        HANDLE io_completion_port
    ) noexcept
        : state_(state)
    {
        if (state_ == nullptr)
            return;

        std::lock_guard<std::mutex> lock(
            state_->stop_signal_mutex
        );

        state_->stop_signal_context = io_completion_port;
        state_->stop_signal = &post_cancellation_completion;

        if (state_->stop_requested.load(
                std::memory_order_acquire
            ))
        {
            state_->stop_signal(
                state_->stop_signal_context
            );
        }
    }

    watcher_stop_signal_guard::~watcher_stop_signal_guard() noexcept
    {
        if (state_ == nullptr)
            return;

        std::lock_guard<std::mutex> lock(
            state_->stop_signal_mutex
        );

        state_->stop_signal = nullptr;
        state_->stop_signal_context = nullptr;
    }
}
