#include "windows_watcher_detail.h"

#include <utility>

namespace fsystem::windows_irr::watcher_common::detail
{
    void handle_deleter::operator()(pointer handle) const noexcept
    {
        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
            return;

        (void)CloseHandle(handle);
    }

    read_operation::read_operation(std::size_t capacity)
        : buffer(capacity)
    {
    }

    directory_watch::directory_watch(std::wstring file_name)
        : watched_file_name(std::move(file_name)),
          operation(event_buffer_capacity)
    {
    }

    DWORD submit_directory_read(directory_watch& directory)
    {
        directory.operation.overlapped = {};
        directory.operation.buffer.resize(event_buffer_capacity);

        const BOOL started = ReadDirectoryChangesW(
            directory.directory_handle.get(),
            directory.operation.buffer.data(),
            static_cast<DWORD>(
                directory.operation.buffer.size()
            ),
            FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME |
            FILE_NOTIFY_CHANGE_DIR_NAME |
            FILE_NOTIFY_CHANGE_LAST_WRITE |
            FILE_NOTIFY_CHANGE_SIZE,
            nullptr,
            &directory.operation.overlapped,
            nullptr
        );

        if (started == FALSE)
        {
            const DWORD error = GetLastError();

            if (error != ERROR_IO_PENDING)
                return error;
        }

        directory.pending = true;
        return ERROR_SUCCESS;
    }

    bool finish_pending_io(
        HANDLE io_completion_port,
        std::vector<directory_watch>& directories,
        WatcherState* state,
        WatcherResult& watcher_result
    ) noexcept
    {
        std::size_t pending_count = 0;

        for (directory_watch& directory : directories)
        {
            if (!directory.pending)
                continue;

            (void)CancelIoEx(
                directory.directory_handle.get(),
                &directory.operation.overlapped
            );
            ++pending_count;
        }

        constexpr DWORD drain_timeout_ms = 10000;

        while (pending_count != 0)
        {
            OVERLAPPED_ENTRY completion{};
            ULONG entries_removed = 0;

            if (!GetQueuedCompletionStatusEx(
                    io_completion_port,
                    &completion,
                    1,
                    &entries_removed,
                    drain_timeout_ms,
                    FALSE
                ))
            {
                if (GetLastError() == WAIT_TIMEOUT)
                {
                    ::OutputDebugStringW(
                        L"watcher: drain timed out waiting for a "
                        L"completion packet; the operation was "
                        L"likely already consumed\n"
                    );

                    watcher_result.event_status =
                        EventStatus::None;

                    watcher_result.error = WAIT_TIMEOUT;

                    if (state != nullptr)
                    {
                        state->cleanup_timed_out.store(
                            true,
                            std::memory_order_release
                        );
                    }
                }

                return false;
            }

            if (entries_removed == 0)
                continue;

            if (
                completion.lpCompletionKey ==
                    cancellation_completion_key
            )
            {
                continue;
            }

            auto* directory = reinterpret_cast<directory_watch*>(
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
            --pending_count;
        }

        return true;
    }
}
