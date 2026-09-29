#include "linux_edit.h"

#include "detail/edit_context.h"
#include "detail/replace_transaction.h"
#include "detail/source_stream.h"
#include "detail/temp_writer.h"
#include "../watcher/edit_watcher.h"

#include <fsystem/edit/edit_detail.h>

#include <cerrno>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace fsystem::linux
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
            edit_watcher& watcher_thread
        )
        {
            edit_item_outcome outcome{};
            EditResult& edit_result = outcome.result;
            edit_result.path = request.path;

            auto& watcher_state = watcher_thread.state();
            bool edit_committed = false;

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

                edit_result.error = ETIMEDOUT;
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
                edit_result.error = EINVAL;
                return outcome;
            }

            edit_result.note = matcher.note();

            if (edit_result.note != EditNote::none)
                return outcome;

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
                outcome.replaced = detail::commit_replace(
                    request.path,
                    source,
                    temp,
                    watcher_state,
                    edit_result.replace_attempted,
                    edit_result.error
                );

                edit_committed = outcome.replaced;
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
                        watcher_thread.result().error ==
                            static_cast<std::uint32_t>(ETIMEDOUT)
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

    EditResults edit_file(const EditRequests& requests)
    {
        EditResults results;

        if (requests.empty())
            return results;

        std::vector<std::filesystem::path> watched_paths;
        watched_paths.reserve(requests.size());

        for (const EditRequest& request : requests)
            watched_paths.push_back(request.path);

        edit_watcher watcher_thread(
            std::span<const std::filesystem::path>(
                watched_paths.data(),
                watched_paths.size()
            )
        );

        std::vector<edit_item_outcome> outcomes;
        outcomes.reserve(requests.size());

        if (watcher_thread.finished_before_ready())
        {
            for (const EditRequest& request : requests)
            {
                edit_item_outcome outcome{};
                outcome.result.path = request.path;

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

                outcomes.push_back(std::move(outcome));
            }
        }
        else
        {
            for (const EditRequest& request : requests)
            {
                outcomes.push_back(edit_one(request, watcher_thread));
                detail::prepare_next_edit(watcher_thread.state());
            }
        }

        /* One cancellation/cleanup for the complete batch. */
        watcher_thread.stop();
        finalize_watcher_result(watcher_thread, outcomes);

        results.reserve(outcomes.size());

        for (edit_item_outcome& outcome : outcomes)
            results.push_back(std::move(outcome.result));

        return results;
    }

    EditResult edit_file(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data
    )
    {
        const EditRequests requests{
            EditRequest{path, old_data, new_data}
        };

        EditResults results = edit_file(requests);

        return results.empty()
            ? EditResult{path}
            : std::move(results.front());
    }
}
