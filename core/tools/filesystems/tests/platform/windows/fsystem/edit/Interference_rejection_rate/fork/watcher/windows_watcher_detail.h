#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_watcher.h"

#include <Windows.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace fsystem::windows_irr::watcher_common::detail
{
    inline constexpr std::size_t event_buffer_capacity = 64 * 1024;
    inline constexpr ULONG max_completion_entries = 10;
    inline constexpr ULONG_PTR directory_completion_key = 1001;
    inline constexpr ULONG_PTR cancellation_completion_key = 1002;

    struct handle_deleter
    {
        using pointer = HANDLE;

        void operator()(pointer handle) const noexcept;
    };

    using unique_handle = std::unique_ptr<void, handle_deleter>;

    struct read_operation
    {
        OVERLAPPED overlapped{};
        std::vector<std::byte> buffer;

        explicit read_operation(std::size_t capacity);
    };

    struct directory_watch
    {
        unique_handle directory_handle{nullptr};
        std::wstring watched_file_name;
        read_operation operation;
        bool pending = false;

        explicit directory_watch(std::wstring file_name);

        directory_watch(const directory_watch&) = delete;
        directory_watch& operator=(const directory_watch&) = delete;
        directory_watch(directory_watch&&) = default;
        directory_watch& operator=(directory_watch&&) = default;
    };

    class watcher_state_guard
    {
    public:
        explicit watcher_state_guard(WatcherState* state) noexcept;

        watcher_state_guard(const watcher_state_guard&) = delete;
        watcher_state_guard& operator=(
            const watcher_state_guard&
        ) = delete;

        ~watcher_state_guard() noexcept;

    private:
        WatcherState* state_;
    };

    class watcher_stop_signal_guard
    {
    public:
        watcher_stop_signal_guard(
            WatcherState* state,
            HANDLE io_completion_port
        ) noexcept;

        watcher_stop_signal_guard(
            const watcher_stop_signal_guard&
        ) = delete;

        watcher_stop_signal_guard& operator=(
            const watcher_stop_signal_guard&
        ) = delete;

        ~watcher_stop_signal_guard() noexcept;

    private:
        WatcherState* state_;
    };

    bool stop_requested(const WatcherState* state) noexcept;

    bool publish_file_changed(WatcherState* state) noexcept;

    struct watch_context
    {
        HANDLE io_completion_port;
        std::vector<directory_watch>& directories;
        DWORD timeout;
        WatcherState* state;
    };

    DWORD submit_directory_read(
        directory_watch& directory
    );

    bool finish_pending_io(
        HANDLE io_completion_port,
        std::vector<directory_watch>& directories,
        WatcherState* state,
        WatcherResult& watcher_result
    ) noexcept;

    void parse_directory_events(
        const std::vector<std::byte>& event_buffer,
        const std::wstring& watched_file_name,
        WatcherState* state,
        bool& target_file_changed
    ) noexcept;

    WatcherResult run_watch_loop(const watch_context& context);
}
