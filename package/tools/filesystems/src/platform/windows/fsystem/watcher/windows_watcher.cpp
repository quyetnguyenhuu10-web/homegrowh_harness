#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "windows_watcher.h"

namespace fsystem::windows
{
    namespace
    {
        struct handle_deleter
        {
            using pointer = HANDLE;

            void operator()(pointer handle) const noexcept
            {
                if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
                    return;

                (void)CloseHandle(handle);
            }
        };

        using unique_handle = std::unique_ptr<void, handle_deleter>;

        constexpr std::size_t event_buffer_capacity = 64 * 1024;
        constexpr ULONG max_completion_entries = 10;
        constexpr ULONG_PTR completion_key = 1001;

        struct read_operation
        {
            OVERLAPPED overlapped{};
            std::vector<std::byte> buffer;

            explicit read_operation(std::size_t capacity)
                : buffer(capacity)
            {
            }
        };
    }

    WatcherResult watcher_file(
        std::filesystem::path path,
        int timeout_f
    )
    {
        WatcherResult watcher_result{};

        if (timeout_f < 0)
        {
            watcher_result.error = ERROR_INVALID_PARAMETER;
            return watcher_result;
        }

        const std::uint32_t timeout =
            static_cast<std::uint32_t>(timeout_f);

        const std::filesystem::path parent_dir =
            path.parent_path();

        const std::wstring watched_file_name =
            path.filename().wstring();

        HANDLE directory_handle = CreateFileW(
            parent_dir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ |
            FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS |
            FILE_FLAG_OVERLAPPED,
            nullptr
        );

        if (directory_handle == INVALID_HANDLE_VALUE)
        {
            watcher_result.error = GetLastError();
            return watcher_result;
        }

        unique_handle directory_guard(directory_handle);

        unique_handle io_completion_port(
            CreateIoCompletionPort(
                directory_handle,
                nullptr,
                completion_key,
                0
            )
        );

        if (io_completion_port.get() == nullptr)
        {
            watcher_result.error = GetLastError();
            return watcher_result;
        }

        read_operation first_operation(event_buffer_capacity);
        read_operation second_operation(event_buffer_capacity);

        read_operation* pending_operation = &first_operation;
        read_operation* spare_operation = &second_operation;

        bool has_pending_io = false;

        const auto submit_read =
            [&](read_operation& operation) -> DWORD
        {
            operation.overlapped = {};
            operation.buffer.resize(event_buffer_capacity);

            const BOOL started = ReadDirectoryChangesW(
                directory_handle,
                operation.buffer.data(),
                static_cast<DWORD>(operation.buffer.size()),
                FALSE,
                FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE,
                nullptr,
                &operation.overlapped,
                nullptr
            );

            if (started == FALSE)
            {
                const DWORD error = GetLastError();

                if (error != ERROR_IO_PENDING)
                    return error;
            }

            return ERROR_SUCCESS;
        };

        const auto finish_pending_io =
            [&](read_operation& operation) noexcept
        {
            (void)CancelIoEx(
                directory_handle,
                &operation.overlapped
            );

            while (true)
            {
                OVERLAPPED_ENTRY completion{};
                ULONG entries_removed = 0;

                if (!GetQueuedCompletionStatusEx(
                    io_completion_port.get(),
                    &completion,
                    1,
                    &entries_removed,
                    INFINITE,
                    FALSE
                ))
                {
                    return;
                }

                if (
                    entries_removed != 0 &&
                    completion.lpOverlapped ==
                        &operation.overlapped
                )
                {
                    return;
                }
            }
        };

        const DWORD submit_error =
            submit_read(*pending_operation);

        if (submit_error != ERROR_SUCCESS)
        {
            watcher_result.error = submit_error;
            return watcher_result;
        }

        has_pending_io = true;

        const DWORD wait_timeout =
            static_cast<DWORD>(timeout);

        const ULONGLONG wait_started_at =
            GetTickCount64();

        try
        {
            while (true)
            {
                const ULONGLONG elapsed =
                    GetTickCount64() - wait_started_at;

                const DWORD remaining_timeout =
                    elapsed >= wait_timeout
                        ? 0
                        : static_cast<DWORD>(
                            wait_timeout - elapsed
                        );

                std::array<
                    OVERLAPPED_ENTRY,
                    max_completion_entries
                > completions{};

                ULONG entries_removed = 0;

                const BOOL got_events =
                    GetQueuedCompletionStatusEx(
                        io_completion_port.get(),
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
                        finish_pending_io(*pending_operation);
                        has_pending_io = false;

                        watcher_result.event_status =
                            watcher_result.events.empty()
                                ? EventStatus::NoEvent
                                : EventStatus::HasEvent;

                        watcher_result.error = ERROR_SUCCESS;
                    }
                    else
                    {
                        finish_pending_io(*pending_operation);
                        has_pending_io = false;

                        watcher_result.event_status =
                            EventStatus::None;

                        watcher_result.error = wait_error;
                    }

                    return watcher_result;
                }

                std::size_t completion_index =
                    entries_removed;

                for (
                    std::size_t index = 0;
                    index < entries_removed;
                    ++index
                )
                {
                    if (
                        completions[index].lpCompletionKey ==
                            completion_key &&
                        completions[index].lpOverlapped ==
                            &pending_operation->overlapped
                    )
                    {
                        completion_index = index;
                        break;
                    }
                }

                if (completion_index == entries_removed)
                    continue;

                const DWORD bytes_transferred =
                    static_cast<DWORD>(
                        completions[completion_index]
                            .dwNumberOfBytesTransferred
                    );

                /*
                 * I/O cũ đã hoàn tất.
                 * Buffer của pending_operation không còn bị Windows ghi nữa.
                 */
                read_operation* completed_operation =
                    pending_operation;

                /*
                 * Đăng ký ReadDirectoryChangesW mới ngay lập tức
                 * bằng buffer và OVERLAPPED khác.
                 */
                read_operation* next_operation =
                    spare_operation;

                has_pending_io = false;

                const DWORD rearm_error =
                    submit_read(*next_operation);

                if (rearm_error != ERROR_SUCCESS)
                {
                    watcher_result.event_status =
                        EventStatus::None;

                    watcher_result.error =
                        rearm_error;

                    return watcher_result;
                }

                pending_operation = next_operation;
                spare_operation = completed_operation;
                has_pending_io = true;

                /*
                 * Chỉ sau khi I/O mới đã được đăng ký,
                 * mới xử lý buffer cũ.
                 */
                if (bytes_transferred == 0)
                    continue;

                if (
                    bytes_transferred >
                    completed_operation->buffer.size()
                )
                {
                    watcher_result.event_status =
                        EventStatus::None;

                    watcher_result.error =
                        ERROR_INVALID_DATA;

                    finish_pending_io(*pending_operation);
                    has_pending_io = false;

                    return watcher_result;
                }

                completed_operation->buffer.resize(
                    bytes_transferred
                );

                watcher_result.events.push_back(
                    completed_operation->buffer
                );

                constexpr std::size_t notify_header_size =
                    offsetof(
                        FILE_NOTIFY_INFORMATION,
                        FileName
                    );

                std::size_t event_offset = 0;

                while (
                    event_offset + notify_header_size <=
                    completed_operation->buffer.size()
                )
                {
                    const auto* notify_information =
                        reinterpret_cast<
                            const FILE_NOTIFY_INFORMATION*
                        >(
                            completed_operation->buffer.data() +
                            event_offset
                        );

                    const std::size_t remaining_bytes =
                        completed_operation->buffer.size() -
                        event_offset;

                    const std::size_t file_name_bytes =
                        notify_information->FileNameLength;

                    if (
                        file_name_bytes % sizeof(WCHAR) != 0 ||
                        file_name_bytes >
                            remaining_bytes -
                            notify_header_size
                    )
                    {
                        break;
                    }

                    const std::size_t record_size =
                        notify_information->NextEntryOffset == 0
                            ? remaining_bytes
                            : notify_information->NextEntryOffset;

                    if (
                        record_size < notify_header_size ||
                        record_size > remaining_bytes
                    )
                    {
                        break;
                    }

                    const std::wstring event_file_name(
                        notify_information->FileName,
                        file_name_bytes / sizeof(WCHAR)
                    );

                    if (event_file_name == watched_file_name)
                    {
                        watcher_result.file_events.emplace_back(
                            completed_operation->buffer.begin() +
                                event_offset,
                            completed_operation->buffer.begin() +
                                event_offset +
                                record_size
                        );
                    }

                    if (
                        notify_information->NextEntryOffset ==
                        0
                    )
                    {
                        break;
                    }

                    event_offset +=
                        notify_information->NextEntryOffset;
                }

                watcher_result.event_status =
                    EventStatus::HasEvent;
            }
        }
        catch (...)
        {
            if (has_pending_io)
            {
                finish_pending_io(*pending_operation);
                has_pending_io = false;
            }

            throw;
        }
    }
}