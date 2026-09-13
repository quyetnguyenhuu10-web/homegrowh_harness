#include <Windows.h>
#include <memory>

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
    }

    WatcherResult watcher_file(
        std::filesystem::path path,
        std::uint32_t timeout
    )
    {
        WatcherResult watcher_result{};

        if (timeout == INFINITE)
        {
            watcher_result.error = ERROR_INVALID_PARAMETER;
            return watcher_result;
        }

        std::filesystem::path parent_dir = path.parent_path();
        const std::wstring watched_file_name = path.filename().wstring();
        HANDLE directory_handle = CreateFileW(
            parent_dir.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ |
            FILE_SHARE_WRITE |
            FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            nullptr
        );

        if (directory_handle == INVALID_HANDLE_VALUE)
        {
            watcher_result.error = GetLastError();
            return watcher_result;
        }

        unique_handle dHandle(directory_handle);

        ULONG_PTR completionKey = 1001; // Key định danh thư mục

        // Tạo IOCP mới đồng thời liên kết directory_handle vào luôn
        unique_handle io_completion_port(CreateIoCompletionPort(
            directory_handle, // Handle thư mục (hoặc Socket, File...)
            nullptr,          // Truyền nullptr để báo hiệu TẠO MỚI một IOCP
            completionKey,    // Key định danh cho handle này
            0                 // Số luồng tối đa (0 = bằng số nhân CPU)
        ));

        if (io_completion_port.get() == nullptr)
        {
            watcher_result.error = GetLastError();
            return watcher_result;
        }

        DWORD bytes_returned = 0;
        OVERLAPPED overlapped{};
        constexpr std::size_t event_buffer_capacity = 64 * 1024;
        std::vector<std::byte> events_temp(event_buffer_capacity);

        const auto finish_pending_io = [&]() noexcept
        {
            (void)CancelIoEx(directory_handle, &overlapped);

            DWORD ignored_bytes = 0;
            (void)GetOverlappedResult(
                directory_handle,
                &overlapped,
                &ignored_bytes,
                TRUE
            );
        };

        const DWORD wait_timeout = static_cast<DWORD>(timeout);
        const ULONGLONG wait_started_at = GetTickCount64();

        while (true) // Giữ timeout là thời gian chờ tổng của watcher, không reset lại sau mỗi lần nhận event.
        {
            events_temp.resize(event_buffer_capacity);

            BOOL result = ReadDirectoryChangesW(
                directory_handle,
                events_temp.data(),
                static_cast<DWORD>(events_temp.size()),
                FALSE, // không watch subdirectory
                FILE_NOTIFY_CHANGE_FILE_NAME |
                FILE_NOTIFY_CHANGE_DIR_NAME |
                FILE_NOTIFY_CHANGE_LAST_WRITE |
                FILE_NOTIFY_CHANGE_SIZE,
                &bytes_returned,
                &overlapped,
                nullptr
            );

            if(result == FALSE)
            {
                const DWORD read_error = GetLastError();
                if (read_error != ERROR_IO_PENDING)
                {
                    watcher_result.error = read_error;
                    return watcher_result;
                }
            }

            const ULONG MAX_ENTRIES = 10;
            std::vector<OVERLAPPED_ENTRY> events_overlap(MAX_ENTRIES);
            ULONG numEntriesRemoved = 0;

            DWORD remaining_timeout = wait_timeout;
            const ULONGLONG elapsed = GetTickCount64() - wait_started_at;
            remaining_timeout = elapsed >= wait_timeout
                ? 0
                : static_cast<DWORD>(wait_timeout - elapsed);

            BOOL get_events = GetQueuedCompletionStatusEx(
                io_completion_port.get(),
                events_overlap.data(),
                MAX_ENTRIES,
                &numEntriesRemoved,
                remaining_timeout,
                FALSE   // fAlertable
            );

            if (get_events == FALSE)
            {
                const DWORD wait_error = GetLastError();
                if (wait_error == WAIT_TIMEOUT)
                {
                    finish_pending_io();
                    watcher_result.event_status = watcher_result.events.empty()
                        ? EventStatus::NoEvent
                        : EventStatus::HasEvent;
                    watcher_result.error = ERROR_SUCCESS;
                }
                else
                {
                    finish_pending_io();
                    watcher_result.event_status = EventStatus::None;
                    watcher_result.error = wait_error;
                }
                return watcher_result;
            }
            // Nếu nhận được sự kiện, lọc sự kiện có mã completionKey = 1001, IOCP
            std::size_t completion_index = numEntriesRemoved;
            for (std::size_t index = 0; index < numEntriesRemoved; ++index)
            {
                if (events_overlap[index].lpCompletionKey == completionKey)
                {
                    completion_index = index;
                    break;
                }
            }

            // completion_index chứa vị trí trong events_overlap;
            // nếu bằng numEntriesRemoved thì không tìm thấy phần tử phù hợp.
            if (completion_index == numEntriesRemoved)
            {
                continue;
            }

            const DWORD bytes_transferred =
                static_cast<DWORD>(events_overlap[completion_index].dwNumberOfBytesTransferred);

            if (bytes_transferred > 0)
            {
                events_temp.resize(bytes_transferred);
                watcher_result.events.push_back(events_temp);

                constexpr std::size_t notify_header_size =
                    offsetof(FILE_NOTIFY_INFORMATION, FileName);
                std::size_t event_offset = 0;

                while (event_offset + notify_header_size <= events_temp.size())
                {
                    const auto* notify_information =
                        reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                            events_temp.data() + event_offset);

                    const std::size_t file_name_bytes =
                        notify_information->FileNameLength;
                    if (file_name_bytes % sizeof(WCHAR) != 0 ||
                        file_name_bytes > events_temp.size() - event_offset - notify_header_size)
                    {
                        break;
                    }

                    const std::size_t record_size =
                        notify_information->NextEntryOffset == 0
                            ? events_temp.size() - event_offset
                            : notify_information->NextEntryOffset;
                    if (record_size < notify_header_size ||
                        record_size > events_temp.size() - event_offset)
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
                            events_temp.begin() + event_offset,
                            events_temp.begin() + event_offset + record_size
                        );
                    }

                    if (notify_information->NextEntryOffset == 0)
                        break;

                    event_offset += notify_information->NextEntryOffset;
                }

                watcher_result.event_status = EventStatus::HasEvent;
                continue;
            }

            // Completion hợp lệ nhưng không có dữ liệu; tiếp tục chờ.
            continue;
        }

    }
}
