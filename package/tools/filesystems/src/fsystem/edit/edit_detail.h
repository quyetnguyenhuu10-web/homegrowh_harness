#pragma once

#include "fsystem/edit/edit.h"
#include "fsystem/watcher/watcher.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <thread>

namespace fsystem::detail
{
    inline constexpr int edit_watcher_timeout =
        std::numeric_limits<int>::max();

    inline constexpr std::uint32_t watcher_thread_exception_error =
        std::numeric_limits<std::uint32_t>::max();

    class watcher_thread_guard
    {
    public:
        watcher_thread_guard(
            const std::filesystem::path& path,
            fsystem::WatcherState& state,
            fsystem::WatcherResult& result,
            std::exception_ptr& exception
        )
            : state_(state),
              thread_(
                  [
                      path,
                      &state,
                      &result,
                      &exception
                  ]()
                  {
                      try
                      {
                          result = fsystem::watcher(
                              path,
                              edit_watcher_timeout,
                              state
                          );
                      }
                      catch (...)
                      {
                          exception = std::current_exception();
                          state.finished.store(
                              true,
                              std::memory_order_release
                          );
                      }
                  }
              )
        {
            while (
                !state_.ready.load(std::memory_order_acquire) &&
                !state_.finished.load(std::memory_order_acquire)
            )
            {
                std::this_thread::yield();
            }
        }

        watcher_thread_guard(const watcher_thread_guard&) = delete;
        watcher_thread_guard& operator=(
            const watcher_thread_guard&
        ) = delete;

        ~watcher_thread_guard() noexcept
        {
            stop();
        }

        void stop() noexcept
        {
            state_.stop_requested.store(
                true,
                std::memory_order_release
            );

            if (thread_.joinable())
                thread_.join();
        }

    private:
        fsystem::WatcherState& state_;
        std::thread thread_;
    };

    inline EditNote find_old_data(
        const std::string& content,
        const std::string& old_data,
        std::size_t& first_occurrence
    )
    {
        first_occurrence = content.find(old_data);

        if (first_occurrence == std::string::npos)
            return EditNote::old_data_not_found;

        const std::size_t second_occurrence =
            content.find(old_data, first_occurrence + 1);

        if (second_occurrence != std::string::npos)
            return EditNote::old_data_appears_more_than_once;

        return EditNote::none;
    }
}
