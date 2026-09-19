#include "windows_watcher.h"

#include "windows_watcher_detail.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace fsystem::windows::watcher_common
{
    WatcherResult watcher_files(
        std::span<const std::filesystem::path> paths,
        int timeout_f,
        WatcherState* state
    )
    {
        WatcherResult watcher_result{};
        detail::watcher_state_guard state_guard(state);

        if (timeout_f < 0 || paths.empty())
        {
            watcher_result.error = ERROR_INVALID_PARAMETER;
            return watcher_result;
        }

        const std::uint32_t timeout =
            static_cast<std::uint32_t>(timeout_f);

        std::vector<detail::directory_watch> directories;
        directories.reserve(paths.size());

        for (const std::filesystem::path& path : paths)
        {
            const std::filesystem::path parent_dir =
                path.parent_path().empty()
                    ? std::filesystem::path(L".")
                    : path.parent_path();

            const std::wstring watched_file_name =
                path.filename().wstring();

            if (watched_file_name.empty())
            {
                watcher_result.error = ERROR_INVALID_PARAMETER;
                return watcher_result;
            }

            directories.emplace_back(watched_file_name);

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

            directories.back().directory_handle.reset(
                directory_handle
            );
        }

        detail::unique_handle io_completion_port(
            CreateIoCompletionPort(
                directories.front().directory_handle.get(),
                nullptr,
                reinterpret_cast<ULONG_PTR>(
                    &directories.front()
                ),
                0
            )
        );

        if (io_completion_port.get() == nullptr)
        {
            watcher_result.error = GetLastError();
            return watcher_result;
        }

        for (std::size_t index = 1; index < directories.size(); ++index)
        {
            if (CreateIoCompletionPort(
                    directories[index].directory_handle.get(),
                    io_completion_port.get(),
                    reinterpret_cast<ULONG_PTR>(
                        &directories[index]
                    ),
                    0
                ) == nullptr)
            {
                watcher_result.error = GetLastError();
                return watcher_result;
            }
        }

        detail::watcher_stop_signal_guard stop_signal_guard(
            state,
            io_completion_port.get()
        );

        const detail::watch_context context{
            io_completion_port.get(),
            directories,
            static_cast<DWORD>(timeout),
            state
        };

        return detail::run_watch_loop(context);
    }

    WatcherResult watcher_file(
        const std::filesystem::path& path,
        int timeout_f,
        WatcherState* state
    )
    {
        return watcher_files(
            std::span<const std::filesystem::path>(&path, 1),
            timeout_f,
            state
        );
    }
}
