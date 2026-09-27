#include "edit_watcher.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <atomic>
#include <utility>

namespace fsystem::windows
{
    edit_watcher::edit_watcher(
        std::span<const std::filesystem::path> paths
    )
        : thread_(
              [this, paths]
              {
                  try
                  {
                      result_ = watcher_common::watcher_files(
                          paths,
                          edit_watcher_timeout,
                          &state_
                      );
                  }
                  catch (...)
                  {
                      exception_ = std::current_exception();
                      state_.finished.store(
                          true,
                          std::memory_order_release
                      );
                  }
              }
          )
    {
        /*
         * This wait also covers a watcher that fails before it can publish
         * ready.  The edit caller can then report the startup error safely.
         */
        while (
            !state_.ready.load(std::memory_order_acquire) &&
            !state_.finished.load(std::memory_order_acquire)
        )
        {
            std::this_thread::yield();
        }
    }

    edit_watcher::~edit_watcher() noexcept
    {
        stop();
    }

    void edit_watcher::stop() noexcept
    {
        bool expected = false;

        if (!stop_started_.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            ))
        {
            return;
        }

        watcher_common::request_watcher_stop(state_);

        const ULONGLONG deadline =
            GetTickCount64() + watcher_stop_timeout_ms;

        while (
            thread_.joinable() &&
            !state_.finished.load(std::memory_order_acquire) &&
            GetTickCount64() < deadline
        )
        {
            std::this_thread::yield();
        }

        if (
            thread_.joinable() &&
            !state_.finished.load(std::memory_order_acquire)
        )
        {
            (void)::TerminateThread(
                thread_.native_handle(),
                watcher_stop_timeout_ms
            );
        }

        if (thread_.joinable())
            thread_.join();
    }

    bool edit_watcher::finished_before_ready() const noexcept
    {
        return state_.finished.load(std::memory_order_acquire) &&
            !state_.ready.load(std::memory_order_acquire);
    }

    bool edit_watcher::cancellation_requested() const noexcept
    {
        return state_.cancel_requested.load(
            std::memory_order_acquire
        );
    }

    bool edit_watcher::file_changed() const noexcept
    {
        return state_.file_changed.load(
            std::memory_order_acquire
        );
    }

    bool edit_watcher::timeout_requested() const noexcept
    {
        return state_.timeout_requested.load(
            std::memory_order_acquire
        );
    }

    bool edit_watcher::timed_out() const noexcept
    {
        return timeout_requested();
    }

    bool edit_watcher::cleanup_timed_out() const noexcept
    {
        return state_.cleanup_timed_out.load(
            std::memory_order_acquire
        );
    }

    bool edit_watcher::has_exception() const noexcept
    {
        return exception_ != nullptr;
    }

    const WatcherResult& edit_watcher::result() const noexcept
    {
        return result_;
    }

    std::exception_ptr edit_watcher::exception() const noexcept
    {
        return exception_;
    }

    watcher_common::WatcherState& edit_watcher::state() noexcept
    {
        return state_;
    }

    const watcher_common::WatcherState& edit_watcher::state() const noexcept
    {
        return state_;
    }
}
