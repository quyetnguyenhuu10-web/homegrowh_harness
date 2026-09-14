#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include "linux_watcher.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fsystem::linux
{
    namespace
    {
        class fd_handle
        {
        public:
            constexpr fd_handle() noexcept = default;
            constexpr fd_handle(std::nullptr_t) noexcept {}
            explicit constexpr fd_handle(int value) noexcept : value_(value) {}

            constexpr int get() const noexcept
            {
                return value_;
            }

            constexpr explicit operator bool() const noexcept
            {
                return value_ >= 0;
            }

            friend constexpr bool operator==(fd_handle lhs, fd_handle rhs) noexcept
            {
                return lhs.value_ == rhs.value_;
            }

            friend constexpr bool operator!=(fd_handle lhs, fd_handle rhs) noexcept
            {
                return !(lhs == rhs);
            }

            friend constexpr bool operator==(fd_handle lhs, std::nullptr_t) noexcept
            {
                return lhs.value_ < 0;
            }

            friend constexpr bool operator==(std::nullptr_t, fd_handle rhs) noexcept
            {
                return rhs == nullptr;
            }

            friend constexpr bool operator!=(fd_handle lhs, std::nullptr_t) noexcept
            {
                return !(lhs == nullptr);
            }

            friend constexpr bool operator!=(std::nullptr_t, fd_handle rhs) noexcept
            {
                return !(rhs == nullptr);
            }

        private:
            int value_{-1};
        };

        struct fd_deleter
        {
            using pointer = fd_handle;

            void operator()(pointer handle) const noexcept
            {
                if (handle != nullptr)
                    (void)::close(handle.get());
            }
        };

        using unique_fd = std::unique_ptr<int, fd_deleter>;

        class inotify_watch_guard
        {
        public:
            inotify_watch_guard(int file_descriptor, int watch_descriptor) noexcept
                : file_descriptor_(file_descriptor),
                  watch_descriptor_(watch_descriptor)
            {
            }

            inotify_watch_guard(const inotify_watch_guard&) = delete;
            inotify_watch_guard& operator=(const inotify_watch_guard&) = delete;

            ~inotify_watch_guard() noexcept
            {
                if (file_descriptor_ >= 0 && watch_descriptor_ >= 0)
                    (void)::inotify_rm_watch(file_descriptor_, watch_descriptor_);
            }

        private:
            int file_descriptor_;
            int watch_descriptor_;
        };

        constexpr std::uint64_t completion_key = 1001;
        constexpr std::size_t event_buffer_capacity = 64 * 1024;
        constexpr int max_epoll_entries = 10;

        constexpr std::uint32_t watch_mask =
            IN_CREATE |
            IN_DELETE |
            IN_MOVED_FROM |
            IN_MOVED_TO |
            IN_MODIFY |
            IN_ATTRIB |
            IN_CLOSE_WRITE;

        std::uint32_t current_errno() noexcept
        {
            return static_cast<std::uint32_t>(errno);
        }
    }

    WatcherResult watcher_file(
        std::filesystem::path path,
        int timeout_f
    )
    {
        WatcherResult watcher_result{};

        // Keep the same public contract as the Windows implementation: the
        // all-bits-set timeout is reserved for an infinite wait and is not
        // accepted by this one-shot watcher API.
        if (timeout_f < 0)
        {
            watcher_result.error = EINVAL;
            return watcher_result;
        }

        std::uint32_t timeout = static_cast<std::uint32_t>(timeout_f);
        const std::filesystem::path parent_directory =
            path.parent_path().empty()
                ? std::filesystem::path(".")
                : path.parent_path();

        const std::string watched_file_name = path.filename().string();

        if (watched_file_name.empty())
        {
            watcher_result.error = EINVAL;
            return watcher_result;
        }

        unique_fd directory_handle{fd_handle{::open(
            parent_directory.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC
        )}};

        if (!directory_handle)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        unique_fd inotify_handle{fd_handle{::inotify_init1(
            IN_NONBLOCK | IN_CLOEXEC
        )}};

        if (!inotify_handle)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        const int watch_descriptor = ::inotify_add_watch(
            inotify_handle.get().get(),
            parent_directory.c_str(),
            watch_mask
        );

        if (watch_descriptor < 0)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        // Closing the inotify descriptor also removes the watch. The explicit
        // guard mirrors cancellation of a pending Windows directory request
        // and makes that cleanup happen before the descriptor is closed.
        inotify_watch_guard watch_guard(
            inotify_handle.get().get(),
            watch_descriptor
        );

        unique_fd event_handle{fd_handle{::epoll_create1(EPOLL_CLOEXEC)}};

        if (!event_handle)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        epoll_event registration{};
        registration.events = EPOLLIN;
        registration.data.u64 = completion_key;

        if (::epoll_ctl(
                event_handle.get().get(),
                EPOLL_CTL_ADD,
                inotify_handle.get().get(),
                &registration
            ) < 0)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        const auto wait_started_at = std::chrono::steady_clock::now();
        const auto wait_deadline = wait_started_at +
            std::chrono::milliseconds(timeout);

        const auto remaining_timeout = [&]() noexcept -> int
        {
            const auto now = std::chrono::steady_clock::now();

            if (now >= wait_deadline)
                return 0;

            const auto remaining_milliseconds =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    wait_deadline - now
                ).count();

            if (remaining_milliseconds <= 0)
                return 1;

            const auto maximum_timeout =
                static_cast<std::int64_t>(
                    std::numeric_limits<int>::max()
                );

            return static_cast<int>(
                remaining_milliseconds > maximum_timeout
                    ? maximum_timeout
                    : remaining_milliseconds
            );
        };

        const auto finish_after_timeout = [&]() noexcept
        {
            watcher_result.event_status = watcher_result.events.empty()
                ? EventStatus::NoEvent
                : EventStatus::HasEvent;
            return watcher_result;
        };

        std::vector<std::byte> events_temp(event_buffer_capacity);
        std::array<epoll_event, max_epoll_entries> events_overlap{};
        bool queue_overflow = false;

        while (true)
        {
            const int wait_timeout = remaining_timeout();

            const int events_ready = ::epoll_wait(
                event_handle.get().get(),
                events_overlap.data(),
                static_cast<int>(events_overlap.size()),
                wait_timeout
            );

            if (events_ready < 0)
            {
                const std::uint32_t wait_error = current_errno();

                if (wait_error == static_cast<std::uint32_t>(EINTR))
                {
                    if (wait_timeout == 0)
                        return finish_after_timeout();

                    continue;
                }

                watcher_result.event_status = EventStatus::None;
                watcher_result.error = wait_error;
                return watcher_result;
            }

            if (events_ready == 0)
            {
                if (queue_overflow)
                    watcher_result.error = EOVERFLOW;
                else
                    watcher_result.error = 0;

                return finish_after_timeout();
            }

            int completion_index = -1;

            for (int index = 0; index < events_ready; ++index)
            {
                if (events_overlap[static_cast<std::size_t>(index)].data.u64 ==
                    completion_key)
                {
                    completion_index = index;
                    break;
                }
            }

            if (completion_index < 0)
                continue;

            const epoll_event& completion =
                events_overlap[static_cast<std::size_t>(completion_index)];

            if ((completion.events & (EPOLLERR | EPOLLHUP)) != 0 &&
                (completion.events & EPOLLIN) == 0)
            {
                watcher_result.event_status = EventStatus::None;
                watcher_result.error = EIO;
                return watcher_result;
            }

            events_temp.resize(event_buffer_capacity);

            const ssize_t bytes_read = ::read(
                inotify_handle.get().get(),
                events_temp.data(),
                events_temp.size()
            );

            if (bytes_read < 0)
            {
                const std::uint32_t read_error = current_errno();

                if (read_error == static_cast<std::uint32_t>(EINTR))
                {
                    if (remaining_timeout() == 0)
                        return finish_after_timeout();

                    continue;
                }

                if (read_error == static_cast<std::uint32_t>(EAGAIN) ||
                    read_error == static_cast<std::uint32_t>(EWOULDBLOCK))
                {
                    continue;
                }

                watcher_result.event_status = EventStatus::None;
                watcher_result.error = read_error;
                return watcher_result;
            }

            if (bytes_read == 0)
            {
                watcher_result.event_status = EventStatus::None;
                watcher_result.error = EIO;
                return watcher_result;
            }

            events_temp.resize(static_cast<std::size_t>(bytes_read));
            watcher_result.events.push_back(events_temp);

            const std::vector<std::byte>& event_buffer =
                watcher_result.events.back();

            constexpr std::size_t event_header_size =
                offsetof(inotify_event, name);

            std::size_t event_offset = 0;

            while (event_offset < event_buffer.size())
            {
                const std::size_t bytes_remaining =
                    event_buffer.size() - event_offset;

                if (bytes_remaining < event_header_size)
                {
                    watcher_result.event_status = EventStatus::None;
                    watcher_result.error = EIO;
                    return watcher_result;
                }

                inotify_event event_header{};
                std::memcpy(
                    &event_header,
                    event_buffer.data() + event_offset,
                    event_header_size
                );

                const std::size_t event_name_bytes =
                    static_cast<std::size_t>(event_header.len);

                if (event_name_bytes > bytes_remaining - event_header_size)
                {
                    watcher_result.event_status = EventStatus::None;
                    watcher_result.error = EIO;
                    return watcher_result;
                }

                const std::size_t event_size =
                    event_header_size + event_name_bytes;

                if ((event_header.mask & IN_Q_OVERFLOW) != 0)
                    queue_overflow = true;

                if ((event_header.mask & IN_IGNORED) != 0 &&
                    event_header.wd == watch_descriptor)
                {
                    watcher_result.event_status = EventStatus::None;
                    watcher_result.error = ENOENT;
                    return watcher_result;
                }

                if (event_header.wd == watch_descriptor &&
                    event_name_bytes != 0)
                {
                    const char* event_name_data = reinterpret_cast<const char*>(
                        event_buffer.data() + event_offset + event_header_size
                    );

                    const void* null_character = std::memchr(
                        event_name_data,
                        '\0',
                        event_name_bytes
                    );

                    const std::size_t event_name_length =
                        null_character == nullptr
                            ? event_name_bytes
                            : static_cast<std::size_t>(
                                static_cast<const char*>(null_character) -
                                event_name_data
                            );

                    if (std::string_view(
                            event_name_data,
                            event_name_length
                        ) == watched_file_name)
                    {
                        watcher_result.file_events.emplace_back(
                            event_buffer.begin() +
                                static_cast<std::ptrdiff_t>(event_offset),
                            event_buffer.begin() +
                                static_cast<std::ptrdiff_t>(
                                    event_offset + event_size
                                )
                        );
                    }
                }

                event_offset += event_size;
            }

            watcher_result.event_status = EventStatus::HasEvent;
        }
    }
}
