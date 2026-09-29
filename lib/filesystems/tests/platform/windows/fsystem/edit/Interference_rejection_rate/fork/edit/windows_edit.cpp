#include "window_edit.h"

#include "detail/edit_context.h"
#include "detail/replace_transaction.h"
#include "detail/source_stream.h"
#include "detail/temp_writer.h"
#include "../watcher/edit_watcher.h"

#include <fsystem/edit/edit_detail.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <thread>
#include <utility>
#include <vector>

namespace fsystem::windows_irr
{
    namespace
    {
        struct edit_item_outcome
        {
            EditResult result;
            bool replaced = false;
        };

        edit_item_outcome edit_one(
            const EditRequest& request,
            int request_index,
            edit_watcher& watcher_thread,
            const IrrProbes& probes
        )
        {
            edit_item_outcome outcome{};
            EditResult& edit_result = outcome.result;
            edit_result.path = request.path;

            auto& watcher_state = watcher_thread.state();
            bool edit_committed = false;

            // Coordination probe dispatcher (runs synchronously
            // on the edit thread; hooks must not throw).
            const auto fire =
                [&](IrrPoint point,
                    const std::filesystem::path& temp_path = {})
            {
                if (!probes.hook)
                    return;

                IrrEvent event;
                event.point = point;
                event.request_index = request_index;
                event.path = request.path;
                event.temp_path = temp_path;
                event.wait_observed = [&](int timeout_ms)
                {
                    using clock = std::chrono::steady_clock;

                    const auto observed = [&]()
                    {
                        return watcher_state.file_changed.load(
                                   std::memory_order_acquire) ||
                            watcher_state.cancel_requested.load(
                                   std::memory_order_acquire) ||
                            watcher_state.timeout_requested.load(
                                   std::memory_order_acquire);
                    };

                    if (observed())
                        return true;

                    const auto deadline =
                        clock::now() +
                        std::chrono::milliseconds(timeout_ms);

                    while (clock::now() < deadline)
                    {
                        if (observed())
                            return true;

                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(1));
                    }

                    return observed();
                };

                probes.hook(event);
            };

            // Fires on every exit path of this request.
            struct request_end_guard
            {
                const decltype(fire)& fire_;

                ~request_end_guard()
                {
                    fire_(IrrPoint::RequestEnd);
                }
            };

            const request_end_guard end_guard{fire};

            fire(IrrPoint::RequestStart);

            const auto apply_watcher_timeout = [&]() noexcept
                -> bool
            {
                if (
                    edit_committed ||
                    !watcher_thread.timed_out() ||
                    watcher_thread.file_changed() ||
                    edit_result.error != 0 ||
                    edit_result.note != EditNote::none
                )
                {
                    return false;
                }

                edit_result.error = WAIT_TIMEOUT;
                edit_result.note = EditNote::timeout;
                return true;
            };

            detail::source_file source;

            if (std::uint32_t error = 0;
                !source.open(request.path, error))
            {
                edit_result.error = error;
                return outcome;
            }

            fire(IrrPoint::SourceOpened);

            if (detail::cancellation_requested(watcher_state))
            {
                if (apply_watcher_timeout())
                    return outcome;

                edit_result.note = EditNote::file_changed;
                return outcome;
            }

            fsystem::detail::chunk_matcher matcher(
                request.old_content
            );

            if (!source.for_each_chunk(
                    [&](std::uint64_t offset, std::string_view chunk)
                    {
                        if (
                            detail::cancellation_requested(
                                watcher_state
                            )
                        )
                        {
                            return false;
                        }

                        return matcher.consume(offset, chunk);
                    },
                    watcher_state,
                    edit_result.error
                ))
            {
                if (apply_watcher_timeout())
                    return outcome;

                if (detail::cancellation_requested(watcher_state))
                {
                    edit_result.note = EditNote::file_changed;
                    return outcome;
                }

                return outcome;
            }

            if (detail::cancellation_requested(watcher_state))
            {
                if (apply_watcher_timeout())
                    return outcome;

                edit_result.note = EditNote::file_changed;
                return outcome;
            }

            matcher.finish(source.size());

            if (matcher.invalid())
            {
                edit_result.error = ERROR_INVALID_DATA;
                return outcome;
            }

            edit_result.note = matcher.note();

            if (edit_result.note != EditNote::none)
                return outcome;

            fire(IrrPoint::ScanDone);

            detail::temporary_file temp;

            const bool prepared = detail::prepare_temp_file(
                request.path,
                source,
                matcher.first_occurrence(),
                request.old_content.size(),
                request.new_content,
                watcher_state,
                temp,
                edit_result.error
            );

            if (prepared)
            {
                // The temp path is captured before commit_replace()
                // because commit clears it on both success and failure.
                const std::filesystem::path ready_temp =
                    temp.file_path();

                fire(IrrPoint::TempReady, ready_temp);

                outcome.replaced = detail::commit_replace(
                    request.path,
                    source,
                    temp,
                    watcher_state,
                    edit_result.replace_attempted,
                    edit_result.error
                );

                edit_committed = outcome.replaced;

                fire(IrrPoint::AfterCommit, ready_temp);
            }

            if (!edit_committed)
                (void)apply_watcher_timeout();

            if (!outcome.replaced)
            {
                if (
                    edit_result.note == EditNote::none &&
                    (
                        watcher_thread.cancellation_requested() ||
                        watcher_thread.file_changed()
                    )
                )
                {
                    edit_result.note = EditNote::file_changed;
                }
            }

            return outcome;
        }

        void finalize_watcher_result(
            const edit_watcher& watcher_thread,
            std::vector<edit_item_outcome>& outcomes
        )
        {
            for (edit_item_outcome& outcome : outcomes)
            {
                EditResult& edit_result = outcome.result;

                if (watcher_thread.has_exception())
                {
                    edit_result.error =
                        watcher_thread_exception_error;
                    continue;
                }

                if (
                    watcher_thread.result().error != 0 &&
                    !watcher_thread.cleanup_timed_out() &&
                    !(
                        outcome.replaced &&
                        watcher_thread.result().error == WAIT_TIMEOUT
                    )
                )
                {
                    if (edit_result.error == 0)
                    {
                        edit_result.error =
                            watcher_thread.result().error;
                    }

                    continue;
                }

                if (
                    !outcome.replaced &&
                    edit_result.note == EditNote::none &&
                    watcher_thread.result().event_status ==
                        EventStatus::HasEvent
                )
                {
                    edit_result.note = EditNote::file_changed;
                }
            }
        }
    }

    EditResults edit_file(
        const EditRequests& requests,
        const IrrProbes& probes
    )
    {
        EditResults results;

        if (requests.empty())
            return results;

        // One dedicated watcher per element.
        //
        // Upstream shares a single watcher (and its cancellation flags)
        // across the whole batch: the first observed change ends the
        // observation window for every remaining request by design.
        // That makes per-element exact intervention impossible inside
        // one batch call, so the fork scopes observation per request
        // instead. Everything below this orchestration (edit_one and
        // the whole watcher implementation) is an untouched clone:
        // each element is processed exactly like an upstream
        // single-request edit.
        std::vector<edit_item_outcome> outcomes;
        outcomes.reserve(requests.size());

        for (int index = 0;
             index < static_cast<int>(requests.size());
             ++index)
        {
            const std::size_t slot =
                static_cast<std::size_t>(index);

            const std::filesystem::path watched[1] = {
                requests[slot].path
            };

            edit_watcher watcher_thread(
                std::span<const std::filesystem::path>(watched, 1)
            );

            // Proactively disabled observation: close the window before
            // the request starts. edit_one below is untouched and sees
            // a watcher that can never report a change.
            if (probes.disable_watcher)
                watcher_thread.stop();

            // The watcher constructor blocks until ready (or failed),
            // so this point means observation is active for this element
            // (unless proactively disabled above).
            if (probes.hook)
            {
                IrrEvent ready_event;
                ready_event.point = IrrPoint::WatcherReady;
                ready_event.request_index = index;
                probes.hook(ready_event);
            }

            std::vector<edit_item_outcome> single;
            single.reserve(1);

            if (watcher_thread.finished_before_ready())
            {
                edit_item_outcome outcome{};
                outcome.result.path = requests[slot].path;

                if (watcher_thread.has_exception())
                {
                    outcome.result.error =
                        watcher_thread_exception_error;
                }
                else if (!watcher_thread.cleanup_timed_out())
                {
                    outcome.result.error =
                        watcher_thread.result().error;
                }

                single.push_back(std::move(outcome));
            }
            else
            {
                single.push_back(
                    edit_one(
                        requests[slot],
                        index,
                        watcher_thread,
                        probes
                    )
                );
            }

            /* Per-element cancellation/cleanup (was once per batch). */
            watcher_thread.stop();
            finalize_watcher_result(watcher_thread, single);

            outcomes.push_back(std::move(single.front()));
        }

        results.reserve(outcomes.size());

        for (edit_item_outcome& outcome : outcomes)
            results.push_back(std::move(outcome.result));

        return results;
    }

    EditResult edit_file(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data,
        const IrrProbes& probes
    )
    {
        const EditRequests requests{
            EditRequest{path, old_data, new_data}
        };

        EditResults results = edit_file(requests, probes);

        return results.empty()
            ? EditResult{path}
            : std::move(results.front());
    }
}
