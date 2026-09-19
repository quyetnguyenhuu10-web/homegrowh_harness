#include "windows_watcher_detail.h"

#include <array>
#include <cstddef>

namespace fsystem::windows_irr::watcher_common::detail
{
    WatcherResult run_watch_loop(const watch_context& context)
    {
        WatcherResult watcher_result{};
        bool target_file_changed = false;

        const auto finish_without_error = [&]()
        {
            watcher_result.event_status =
                target_file_changed
                    ? EventStatus::HasEvent
                    : EventStatus::NoEvent;
            watcher_result.error = ERROR_SUCCESS;
            return watcher_result;
        };

        for (directory_watch& directory : context.directories)
        {
            const DWORD submit_error =
                submit_directory_read(directory);

            if (submit_error != ERROR_SUCCESS)
            {
                watcher_result.error = submit_error;
                (void)finish_pending_io(
                    context.io_completion_port,
                    context.directories,
                    context.state,
                    watcher_result
                );
                return watcher_result;
            }
        }

        if (context.state != nullptr)
        {
            context.state->ready.store(
                true,
                std::memory_order_release
            );
        }

        const ULONGLONG wait_started_at =
            GetTickCount64();

        try
        {
            while (true)
            {
                const ULONGLONG elapsed =
                    GetTickCount64() - wait_started_at;

                const DWORD remaining_timeout =
                    elapsed >= context.timeout
                        ? 0
                        : static_cast<DWORD>(
                            context.timeout - elapsed
                        );

                std::array<
                    OVERLAPPED_ENTRY,
                    max_completion_entries
                > completions{};

                ULONG entries_removed = 0;

                const BOOL got_events =
                    GetQueuedCompletionStatusEx(
                        context.io_completion_port,
                        completions.data(),
                        max_completion_entries,
                        &entries_removed,
                        remaining_timeout,
                        FALSE
                    );

                if (got_events == FALSE)
                {
                    const DWORD wait_error = GetLastError();

                    if (wait_error == WAIT_TIMEOUT)
                    {
                        if (!stop_requested(context.state))
                        {
                            (void)request_edit_timeout(
                                context.state
                            );
                        }

                        if (!finish_pending_io(
                                context.io_completion_port,
                                context.directories,
                                context.state,
                                watcher_result
                            ))
                        {
                            return watcher_result;
                        }

                        return finish_without_error();
                    }

                    if (!finish_pending_io(
                            context.io_completion_port,
                            context.directories,
                            context.state,
                            watcher_result
                        ))
                    {
                        return watcher_result;
                    }

                    watcher_result.event_status =
                        EventStatus::None;
                    watcher_result.error = wait_error;
                    return watcher_result;
                }

                if (entries_removed == 0)
                    continue;

                std::size_t cancellation_index =
                    entries_removed;

                std::size_t first_directory_index =
                    entries_removed;

                for (
                    std::size_t index = 0;
                    index < entries_removed;
                    ++index
                )
                {
                    if (
                        completions[index].lpCompletionKey ==
                            cancellation_completion_key &&
                        cancellation_index == entries_removed
                    )
                    {
                        cancellation_index = index;
                    }

                    if (
                        completions[index].lpCompletionKey !=
                            cancellation_completion_key &&
                        first_directory_index == entries_removed
                    )
                    {
                        first_directory_index = index;
                    }
                }

                if (
                    cancellation_index < first_directory_index
                )
                {
                    if (!finish_pending_io(
                            context.io_completion_port,
                            context.directories,
                            context.state,
                            watcher_result
                        ))
                    {
                        return watcher_result;
                    }

                    return finish_without_error();
                }

                for (
                    std::size_t index = 0;
                    index < entries_removed;
                    ++index
                )
                {
                    const OVERLAPPED_ENTRY& completion =
                        completions[index];

                    if (
                        completion.lpCompletionKey ==
                            cancellation_completion_key
                    )
                    {
                        continue;
                    }

                    auto* directory =
                        reinterpret_cast<directory_watch*>(
                            completion.lpCompletionKey
                        );

                    if (
                        directory == nullptr ||
                        completion.lpOverlapped !=
                            &directory->operation.overlapped ||
                        !directory->pending
                    )
                    {
                        continue;
                    }

                    directory->pending = false;

                    const DWORD bytes_transferred =
                        static_cast<DWORD>(
                            completion.dwNumberOfBytesTransferred
                        );

                    if (
                        bytes_transferred != 0 &&
                        bytes_transferred >
                            directory->operation.buffer.size()
                    )
                    {
                        watcher_result.event_status =
                            EventStatus::None;
                        watcher_result.error =
                            ERROR_INVALID_DATA;

                        (void)finish_pending_io(
                            context.io_completion_port,
                            context.directories,
                            context.state,
                            watcher_result
                        );
                        return watcher_result;
                    }

                    if (bytes_transferred != 0)
                    {
                        directory->operation.buffer.resize(
                            bytes_transferred
                        );

                        parse_directory_events(
                            directory->operation.buffer,
                            directory->watched_file_name,
                            context.state,
                            target_file_changed
                        );
                    }

                    if (target_file_changed)
                    {
                        if (!finish_pending_io(
                                context.io_completion_port,
                                context.directories,
                                context.state,
                                watcher_result
                            ))
                        {
                            return watcher_result;
                        }

                        return finish_without_error();
                    }

                    const DWORD rearm_error =
                        submit_directory_read(*directory);

                    if (rearm_error != ERROR_SUCCESS)
                    {
                        watcher_result.event_status =
                            EventStatus::None;
                        watcher_result.error = rearm_error;

                        (void)finish_pending_io(
                            context.io_completion_port,
                            context.directories,
                            context.state,
                            watcher_result
                        );
                        return watcher_result;
                    }
                }

                if (
                    cancellation_index != entries_removed ||
                    stop_requested(context.state)
                )
                {
                    if (!finish_pending_io(
                            context.io_completion_port,
                            context.directories,
                            context.state,
                            watcher_result
                        ))
                    {
                        return watcher_result;
                    }

                    return finish_without_error();
                }
            }
        }
        catch (...)
        {
            (void)finish_pending_io(
                context.io_completion_port,
                context.directories,
                context.state,
                watcher_result
            );
            throw;
        }
    }
}
