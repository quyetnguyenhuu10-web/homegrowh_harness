#include "linux_edit.h"

#include "detail/edit_context.h"
#include "detail/replace_transaction.h"
#include "detail/source_stream.h"
#include "detail/temp_writer.h"

#include <fsystem/edit/edit_detail.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <exception>
#include <string_view>

namespace fsystem::linux
{
    EditResult edit_file(
        const std::filesystem::path& path,
        const std::string& old_data,
        const std::string& new_data
    )
    {
        EditResult edit_result{};

        fsystem::WatcherState watcher_state;
        fsystem::WatcherResult watcher_result{};
        std::exception_ptr watcher_exception;

        fsystem::detail::watcher_thread_guard watcher_thread(
            path,
            watcher_state,
            watcher_result,
            watcher_exception
        );

        /* Preserve the early-stop-before-ready handling. */
        if (
            watcher_state.finished.load(std::memory_order_acquire) &&
            !watcher_state.ready.load(std::memory_order_acquire)
        )
        {
            watcher_thread.stop();

            edit_result.error = watcher_exception != nullptr
                ? fsystem::detail::watcher_thread_exception_error
                : watcher_result.error;

            return edit_result;
        }

        detail::source_file source;

        if (std::uint32_t error = 0;
            !source.open(path, error))
        {
            watcher_thread.stop();
            edit_result.error = error;
            return edit_result;
        }

        /* The watcher may have detected a change while opening the source. */
        if (detail::cancellation_requested(watcher_state))
        {
            watcher_thread.stop();
            edit_result.note = EditNote::file_changed;
            return edit_result;
        }

        fsystem::detail::chunk_matcher matcher(old_data);

        if (!source.for_each_chunk(
                [&](std::uint64_t offset, std::string_view chunk)
                {
                    if (detail::cancellation_requested(watcher_state))
                        return false;

                    return matcher.consume(offset, chunk);
                },
                watcher_state,
                edit_result.error
            ))
        {
            watcher_thread.stop();

            if (detail::cancellation_requested(watcher_state))
            {
                edit_result.note = EditNote::file_changed;
                return edit_result;
            }

            return edit_result;
        }

        /* A change can arrive after the last chunk and before finalization. */
        if (detail::cancellation_requested(watcher_state))
        {
            watcher_thread.stop();
            edit_result.note = EditNote::file_changed;
            return edit_result;
        }

        matcher.finish(source.size());

        if (matcher.invalid())
        {
            watcher_thread.stop();
            edit_result.error = EINVAL;
            return edit_result;
        }

        edit_result.note = matcher.note();

        if (edit_result.note != EditNote::none)
        {
            watcher_thread.stop();
            return edit_result;
        }

        detail::temporary_file temp;

        const bool prepared = detail::prepare_temp_file(
            path,
            source,
            matcher.first_occurrence(),
            old_data.size(),
            new_data,
            watcher_state,
            temp,
            edit_result.error
        );

        bool replaced = false;

        if (prepared)
        {
            replaced = detail::commit_replace(
                path,
                source,
                temp,
                watcher_state,
                edit_result.replace_attempted,
                edit_result.error
            );
        }

        /* Keep the watcher alive through the complete commit attempt. */
        watcher_thread.stop();

        if (watcher_exception != nullptr)
        {
            edit_result.error =
                fsystem::detail::watcher_thread_exception_error;
            return edit_result;
        }

        if (watcher_result.error != 0)
        {
            edit_result.error = watcher_result.error;
            return edit_result;
        }

        if (!replaced)
        {
            if (
                watcher_state.cancel_requested.load(
                    std::memory_order_acquire
                ) ||
                watcher_state.file_changed.load(
                    std::memory_order_acquire
                ) ||
                watcher_result.event_status == EventStatus::HasEvent
            )
            {
                edit_result.note = EditNote::file_changed;
            }

            return edit_result;
        }

        return edit_result;
    }
}
