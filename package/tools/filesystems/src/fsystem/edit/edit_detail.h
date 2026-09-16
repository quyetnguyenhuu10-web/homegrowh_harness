#pragma once

#include "fsystem/edit/edit.h"
#include "fsystem/watcher/watcher.h"

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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
            fsystem::request_watcher_stop(state_);

            if (thread_.joinable())
                thread_.join();
        }

    private:
        fsystem::WatcherState& state_;
        std::thread thread_;
    };

    class chunk_matcher
    {
    public:
        static constexpr std::uint64_t no_occurrence =
            std::numeric_limits<std::uint64_t>::max();

        explicit chunk_matcher(std::string old_data)
            : pattern_(std::move(old_data))
        {
        }

        chunk_matcher(const chunk_matcher&) = delete;
        chunk_matcher& operator=(const chunk_matcher&) = delete;

        bool consume(
            std::uint64_t chunk_offset,
            std::string_view chunk
        )
        {
            if (invalid_ || duplicate_)
                return false;

            if (chunk_offset != next_chunk_offset_)
            {
                invalid_ = true;
                return false;
            }

            if (!pattern_.empty())
            {
                std::vector<checkpoint> next_checkpoints;
                next_checkpoints.reserve(checkpoints_.size() + 1);

                for (const checkpoint& candidate : checkpoints_)
                {
                    std::size_t consumed = 0;

                    while (
                        candidate.matched + consumed < pattern_.size() &&
                        consumed < chunk.size() &&
                        pattern_[candidate.matched + consumed] ==
                            chunk[consumed]
                    )
                    {
                        ++consumed;
                    }

                    if (candidate.matched + consumed == pattern_.size())
                    {
                        record(candidate.start);

                        if (duplicate_)
                            return false;
                    }
                    else if (consumed == chunk.size())
                    {
                        next_checkpoints.push_back(
                            checkpoint{
                                candidate.start,
                                candidate.matched + consumed
                            }
                        );
                    }
                }

                std::size_t occurrence = chunk.find(pattern_);

                while (occurrence != std::string_view::npos)
                {
                    record(chunk_offset + occurrence);

                    if (duplicate_)
                        return false;

                    occurrence = chunk.find(
                        pattern_,
                        occurrence + 1
                    );
                }

                const std::size_t minimum_start =
                    chunk.size() > pattern_.size()
                        ? chunk.size() - pattern_.size() + 1
                        : 0;

                for (
                    std::size_t start = minimum_start;
                    start < chunk.size();
                    ++start
                )
                {
                    const std::size_t matched = chunk.size() - start;

                    if (
                        matched < pattern_.size() &&
                        chunk.compare(
                            start,
                            matched,
                            pattern_,
                            0,
                            matched
                        ) == 0
                    )
                    {
                        next_checkpoints.push_back(
                            checkpoint{
                                chunk_offset + start,
                                matched
                            }
                        );
                    }
                }

                checkpoints_ = std::move(next_checkpoints);
            }

            next_chunk_offset_ = chunk_offset + chunk.size();
            return true;
        }

        void finish(std::uint64_t file_size)
        {
            if (invalid_ || duplicate_)
                return;

            if (pattern_.empty())
            {
                first_occurrence_ = 0;
                duplicate_ = file_size != 0;
                note_ = duplicate_
                    ? EditNote::old_data_appears_more_than_once
                    : EditNote::none;
                return;
            }

            note_ = first_occurrence_ == no_occurrence
                ? EditNote::old_data_not_found
                : EditNote::none;
        }

        bool invalid() const noexcept
        {
            return invalid_;
        }

        bool duplicate() const noexcept
        {
            return duplicate_;
        }

        EditNote note() const noexcept
        {
            return note_;
        }

        std::uint64_t first_occurrence() const noexcept
        {
            return first_occurrence_;
        }

    private:
        struct checkpoint
        {
            std::uint64_t start;
            std::size_t matched;
        };

        void record(std::uint64_t start) noexcept
        {
            if (first_occurrence_ == no_occurrence)
            {
                first_occurrence_ = start;
                return;
            }

            if (first_occurrence_ != start)
            {
                duplicate_ = true;
                note_ = EditNote::old_data_appears_more_than_once;
            }
        }

        std::string pattern_;
        std::vector<checkpoint> checkpoints_;
        std::uint64_t next_chunk_offset_ = 0;
        std::uint64_t first_occurrence_ = no_occurrence;
        EditNote note_ = EditNote::none;
        bool invalid_ = false;
        bool duplicate_ = false;
    };
}
