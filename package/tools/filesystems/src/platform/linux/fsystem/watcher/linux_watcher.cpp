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
#include <mutex>
#include <sys/eventfd.h>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fsystem::linux::watcher_common
{
    namespace
    {
        class fd_handle
        {
        public:
            constexpr fd_handle() noexcept = default;
            constexpr fd_handle(std::nullptr_t) noexcept {}
            explicit constexpr fd_handle(int value) noexcept
                : value_(value)
            {
            }

            constexpr int get() const noexcept
            {
                return value_;
            }

            constexpr explicit operator bool() const noexcept
            {
                return value_ >= 0;
            }

            friend constexpr bool operator==(
                fd_handle lhs,
                fd_handle rhs
            ) noexcept
            {
                return lhs.value_ == rhs.value_;
            }

            friend constexpr bool operator!=(
                fd_handle lhs,
                fd_handle rhs
            ) noexcept
            {
                return !(lhs == rhs);
            }

            friend constexpr bool operator==(
                fd_handle lhs,
                std::nullptr_t
            ) noexcept
            {
                return lhs.value_ < 0;
            }

            friend constexpr bool operator==(
                std::nullptr_t,
                fd_handle rhs
            ) noexcept
            {
                return rhs == nullptr;
            }

            friend constexpr bool operator!=(
                fd_handle lhs,
                std::nullptr_t
            ) noexcept
            {
                return !(lhs == nullptr);
            }

            friend constexpr bool operator!=(
                std::nullptr_t,
                fd_handle rhs
            ) noexcept
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
            inotify_watch_guard(
                int file_descriptor,
                int watch_descriptor
            ) noexcept
                : file_descriptor_(file_descriptor),
                  watch_descriptor_(watch_descriptor)
            {
            }

            inotify_watch_guard(const inotify_watch_guard&) = delete;
            inotify_watch_guard& operator=(
                const inotify_watch_guard&
            ) = delete;

            ~inotify_watch_guard() noexcept
            {
                if (
                    file_descriptor_ >= 0 &&
                    watch_descriptor_ >= 0
                )
                {
                    (void)::inotify_rm_watch(
                        file_descriptor_,
                        watch_descriptor_
                    );
                }
            }

        private:
            int file_descriptor_;
            int watch_descriptor_;
        };

        constexpr std::uint64_t directory_completion_key = 1001;
        constexpr std::uint64_t cancellation_completion_key = 1002;
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

        class watcher_state_guard
        {
        public:
            explicit watcher_state_guard(WatcherState* state) noexcept
                : state_(state)
            {
                if (state_ == nullptr)
                    return;

                state_->ready.store(false, std::memory_order_release);
                state_->finished.store(false, std::memory_order_release);
                state_->file_changed.store(
                    false,
                    std::memory_order_release
                );

                /*
                 * A WatcherState may be reused by the caller.
                 *
                 * cancel_requested belongs to the current watcher
                 * lifetime, so it must start in the non-cancelled state.
                 */
                state_->cancel_requested.store(
                    false,
                    std::memory_order_release
                );

                state_->timeout_requested.store(
                    false,
                    std::memory_order_release
                );

                state_->cleanup_timed_out.store(
                    false,
                    std::memory_order_release
                );

                state_->edit_commit_phase.store(
                    EditCommitPhase::Watching,
                    std::memory_order_release
                );
            }

            watcher_state_guard(const watcher_state_guard&) = delete;
            watcher_state_guard& operator=(
                const watcher_state_guard&
            ) = delete;

            ~watcher_state_guard() noexcept
            {
                if (state_ != nullptr)
                {
                    state_->finished.store(
                        true,
                        std::memory_order_release
                    );
                }
            }

        private:
            WatcherState* state_;
        };

        bool stop_requested(const WatcherState* state) noexcept
        {
            return state != nullptr &&
                state->stop_requested.load(std::memory_order_acquire);
        }

        bool publish_file_changed(WatcherState* state) noexcept
        {
            if (state == nullptr)
                return true;

            if (
                state->edit_commit_phase.load(
                    std::memory_order_acquire
                ) != EditCommitPhase::Watching
            )
            {
                return false;
            }

            /*
             * Publish the observable result before the active
             * cancellation signal, matching the Windows watcher.
             */
            state->file_changed.store(
                true,
                std::memory_order_release
            );

            state->cancel_requested.store(
                true,
                std::memory_order_release
            );

            return true;
        }

        struct cancellation_signal_context
        {
            int file_descriptor;
        };

        void post_cancellation_event(void* context) noexcept
        {
            const auto* cancellation =
                static_cast<const cancellation_signal_context*>(context);

            if (cancellation == nullptr || cancellation->file_descriptor < 0)
                return;

            const std::uint64_t signal = 1;

            while (
                ::write(
                    cancellation->file_descriptor,
                    &signal,
                    sizeof(signal)
                ) < 0 &&
                errno == EINTR
            )
            {
            }
        }

        class watcher_stop_signal_guard
        {
        public:
            watcher_stop_signal_guard(
                WatcherState* state,
                cancellation_signal_context* context
            ) noexcept
                : state_(state)
            {
                if (state_ == nullptr)
                    return;

                std::lock_guard<std::mutex> lock(
                    state_->stop_signal_mutex
                );

                state_->stop_signal_context = context;
                state_->stop_signal = &post_cancellation_event;

                if (state_->stop_requested.load(
                        std::memory_order_acquire
                    ))
                {
                    state_->stop_signal(
                        state_->stop_signal_context
                    );
                }
            }

            watcher_stop_signal_guard(
                const watcher_stop_signal_guard&
            ) = delete;
            watcher_stop_signal_guard& operator=(
                const watcher_stop_signal_guard&
            ) = delete;

            ~watcher_stop_signal_guard() noexcept
            {
                if (state_ == nullptr)
                    return;

                std::lock_guard<std::mutex> lock(
                    state_->stop_signal_mutex
                );

                state_->stop_signal = nullptr;
                state_->stop_signal_context = nullptr;
            }

        private:
            WatcherState* state_;
        };
    }

    void request_watcher_stop(WatcherState& state) noexcept
    {
        state.stop_requested.store(
            true,
            std::memory_order_release
        );

        std::lock_guard<std::mutex> lock(
            state.stop_signal_mutex
        );

        if (state.stop_signal != nullptr)
        {
            state.stop_signal(state.stop_signal_context);
        }
    }

    bool request_edit_timeout(WatcherState* state) noexcept
    {
        if (state == nullptr)
            return false;

        if (
            state->cancel_requested.load(std::memory_order_acquire) ||
            state->file_changed.load(std::memory_order_acquire)
        )
        {
            return false;
        }

        EditCommitPhase expected = EditCommitPhase::Watching;

        if (!state->edit_commit_phase.compare_exchange_strong(
                expected,
                EditCommitPhase::TimedOut,
                std::memory_order_acq_rel,
                std::memory_order_acquire
            ))
        {
            return false;
        }

        state->timeout_requested.store(
            true,
            std::memory_order_release
        );

        state->cancel_requested.store(
            true,
            std::memory_order_release
        );

        return true;
    }

    bool begin_edit_commit(WatcherState& state) noexcept
    {
        if (
            state.timeout_requested.load(std::memory_order_acquire) ||
            state.file_changed.load(std::memory_order_acquire) ||
            state.cancel_requested.load(std::memory_order_acquire)
        )
        {
            return false;
        }

        EditCommitPhase expected = state.edit_commit_phase.load(
            std::memory_order_acquire
        );

        while (
            expected == EditCommitPhase::Watching ||
            expected == EditCommitPhase::Committed
        )
        {
            if (state.edit_commit_phase.compare_exchange_weak(
                    expected,
                    EditCommitPhase::Committing,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                ))
            {
                return true;
            }

            if (
                state.timeout_requested.load(
                    std::memory_order_acquire
                ) ||
                state.file_changed.load(
                    std::memory_order_acquire
                ) ||
                state.cancel_requested.load(
                    std::memory_order_acquire
                )
            )
            {
                return false;
            }
        }

        return false;
    }

    void mark_edit_committed(WatcherState& state) noexcept
    {
        state.edit_commit_phase.store(
            EditCommitPhase::Committed,
            std::memory_order_release
        );
    }

    void prepare_next_edit(WatcherState& state) noexcept
    {
        if (
            state.timeout_requested.load(std::memory_order_acquire) ||
            state.file_changed.load(std::memory_order_acquire) ||
            state.cancel_requested.load(std::memory_order_acquire)
        )
        {
            return;
        }

        EditCommitPhase phase = state.edit_commit_phase.load(
            std::memory_order_acquire
        );

        while (phase == EditCommitPhase::Committing)
        {
            if (state.edit_commit_phase.compare_exchange_weak(
                    phase,
                    EditCommitPhase::Watching,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire
                ))
            {
                return;
            }

            if (
                state.timeout_requested.load(
                    std::memory_order_acquire
                ) ||
                state.file_changed.load(
                    std::memory_order_acquire
                ) ||
                state.cancel_requested.load(
                    std::memory_order_acquire
                )
            )
            {
                return;
            }
        }
    }

    WatcherResult watcher_files(
        std::span<const std::filesystem::path> paths,
        int timeout_f,
        WatcherState* state
    )
    {
        WatcherResult watcher_result{};
        watcher_state_guard state_guard(state);

        if (timeout_f < 0 || paths.empty())
        {
            watcher_result.error = EINVAL;
            return watcher_result;
        }

        const std::uint32_t timeout =
            static_cast<std::uint32_t>(timeout_f);

        unique_fd inotify_handle{fd_handle{::inotify_init1(
            IN_NONBLOCK |
            IN_CLOEXEC
        )}};

        if (!inotify_handle)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        std::vector<unique_fd> directory_handles;
        directory_handles.reserve(paths.size());

        std::vector<int> watch_descriptors;
        watch_descriptors.reserve(paths.size());

        std::vector<std::string> watched_file_names;
        watched_file_names.reserve(paths.size());

        for (const std::filesystem::path& path : paths)
        {
            const std::filesystem::path parent_directory =
                path.parent_path().empty()
                    ? std::filesystem::path(".")
                    : path.parent_path();

            const std::string watched_file_name =
                path.filename().string();

            if (watched_file_name.empty())
            {
                watcher_result.error = EINVAL;
                return watcher_result;
            }

            directory_handles.emplace_back(fd_handle{
                ::open(
                    parent_directory.c_str(),
                    O_RDONLY |
                    O_DIRECTORY |
                    O_CLOEXEC
                )
            });

            if (!directory_handles.back())
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

            watch_descriptors.push_back(watch_descriptor);
            watched_file_names.push_back(watched_file_name);
        }

        unique_fd event_handle{fd_handle{
            ::epoll_create1(EPOLL_CLOEXEC)
        }};

        if (!event_handle)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        unique_fd cancellation_handle{fd_handle{}};

        if (state != nullptr)
        {
            cancellation_handle.reset(fd_handle{
                ::eventfd(
                    0,
                    EFD_NONBLOCK |
                    EFD_CLOEXEC
                )
            });

            if (!cancellation_handle)
            {
                watcher_result.error = current_errno();
                return watcher_result;
            }
        }

        epoll_event directory_registration{};
        directory_registration.events =
            EPOLLIN |
            EPOLLONESHOT;
        directory_registration.data.u64 =
            directory_completion_key;

        if (::epoll_ctl(
                event_handle.get().get(),
                EPOLL_CTL_ADD,
                inotify_handle.get().get(),
                &directory_registration
            ) < 0)
        {
            watcher_result.error = current_errno();
            return watcher_result;
        }

        if (state != nullptr)
        {
            epoll_event cancellation_registration{};
            cancellation_registration.events = EPOLLIN;
            cancellation_registration.data.u64 =
                cancellation_completion_key;

            if (::epoll_ctl(
                    event_handle.get().get(),
                    EPOLL_CTL_ADD,
                    cancellation_handle.get().get(),
                    &cancellation_registration
                ) < 0)
            {
                watcher_result.error = current_errno();
                return watcher_result;
            }
        }

        cancellation_signal_context cancellation_context{
            state != nullptr
                ? cancellation_handle.get().get()
                : -1
        };

        watcher_stop_signal_guard stop_signal_guard(
            state,
            state != nullptr
                ? &cancellation_context
                : nullptr
        );

        if (state != nullptr)
        {
            state->ready.store(true, std::memory_order_release);
        }

        const auto rearm_epoll = [&]() noexcept
            -> std::uint32_t
        {
            if (::epoll_ctl(
                    event_handle.get().get(),
                    EPOLL_CTL_MOD,
                    inotify_handle.get().get(),
                    &directory_registration
                ) < 0)
            {
                return current_errno();
            }

            return 0;
        };

        const auto wait_started_at =
            std::chrono::steady_clock::now();

        const auto wait_deadline =
            wait_started_at +
            std::chrono::milliseconds(timeout);

        const auto remaining_timeout = [&]() noexcept
            -> int
        {
            const auto now =
                std::chrono::steady_clock::now();

            if (now >= wait_deadline)
                return 0;

            const auto remaining_milliseconds =
                std::chrono::duration_cast<
                    std::chrono::milliseconds
                >(
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

        bool target_file_changed = false;

        const auto finish_after_timeout = [&]()
            -> WatcherResult
        {
            watcher_result.event_status =
                target_file_changed
                    ? EventStatus::HasEvent
                    : EventStatus::NoEvent;

            return watcher_result;
        };

        std::vector<std::byte> events_temp(
            event_buffer_capacity
        );

        std::array<
            epoll_event,
            max_epoll_entries
        > events_overlap{};

        bool queue_overflow = false;

        while (true)
        {
            const int wait_timeout = remaining_timeout();

            const int events_ready = ::epoll_wait(
                event_handle.get().get(),
                events_overlap.data(),
                static_cast<int>(
                    events_overlap.size()
                ),
                wait_timeout
            );

            if (events_ready < 0)
            {
                const std::uint32_t wait_error =
                    current_errno();

                if (
                    wait_error ==
                    static_cast<std::uint32_t>(EINTR)
                )
                {
                    if (wait_timeout == 0)
                    {
                        if (!stop_requested(state))
                            (void)request_edit_timeout(state);

                        return finish_after_timeout();
                    }

                    continue;
                }

                watcher_result.event_status =
                    EventStatus::None;

                watcher_result.error = wait_error;
                return watcher_result;
            }

            if (events_ready == 0)
            {
                if (
                    !queue_overflow &&
                    !stop_requested(state)
                )
                {
                    (void)request_edit_timeout(state);
                }

                watcher_result.error =
                    queue_overflow
                        ? EOVERFLOW
                        : 0;

                if (stop_requested(state))
                    watcher_result.error = 0;

                return finish_after_timeout();
            }

            int directory_index = -1;
            int cancellation_index = -1;

            for (
                int index = 0;
                index < events_ready;
                ++index
            )
            {
                const std::uint64_t completion_key =
                    events_overlap[
                        static_cast<std::size_t>(index)
                    ].data.u64;

                if (
                    completion_key ==
                    directory_completion_key &&
                    directory_index < 0
                )
                {
                    directory_index = index;
                }

                if (
                    completion_key ==
                    cancellation_completion_key &&
                    cancellation_index < 0
                )
                {
                    cancellation_index = index;
                }
            }

            const auto consume_cancellation_event = [&]() noexcept
            {
                if (!cancellation_handle)
                    return;

                std::uint64_t signal = 0;

                while (
                    ::read(
                        cancellation_handle.get().get(),
                        &signal,
                        sizeof(signal)
                    ) < 0 &&
                    errno == EINTR
                )
                {
                }
            };

            if (
                cancellation_index >= 0 &&
                (
                    directory_index < 0 ||
                    cancellation_index < directory_index
                )
            )
            {
                consume_cancellation_event();
                watcher_result.error = 0;
                return finish_after_timeout();
            }

            if (directory_index < 0)
            {
                if (cancellation_index >= 0)
                {
                    consume_cancellation_event();
                    watcher_result.error = 0;
                    return finish_after_timeout();
                }

                const std::uint32_t rearm_error =
                    rearm_epoll();

                if (rearm_error != 0)
                {
                    watcher_result.event_status =
                        EventStatus::None;

                    watcher_result.error =
                        rearm_error;

                    return watcher_result;
                }

                continue;
            }

            const epoll_event& completion =
                events_overlap[
                    static_cast<std::size_t>(
                        directory_index
                    )
                ];

            if (
                (completion.events & (EPOLLERR | EPOLLHUP)) != 0 &&
                (completion.events & EPOLLIN) == 0
            )
            {
                watcher_result.event_status =
                    EventStatus::None;

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
                const std::uint32_t read_error =
                    current_errno();

                if (
                    read_error ==
                    static_cast<std::uint32_t>(EINTR)
                )
                {
                    const std::uint32_t rearm_error =
                        rearm_epoll();

                    if (rearm_error != 0)
                    {
                        watcher_result.event_status =
                            EventStatus::None;

                        watcher_result.error =
                            rearm_error;

                        return watcher_result;
                    }

                    if (remaining_timeout() == 0)
                    {
                        if (!stop_requested(state))
                            (void)request_edit_timeout(state);

                        return finish_after_timeout();
                    }

                    continue;
                }

                if (
                    read_error ==
                        static_cast<std::uint32_t>(EAGAIN) ||
                    read_error ==
                        static_cast<std::uint32_t>(EWOULDBLOCK)
                )
                {
                    const std::uint32_t rearm_error =
                        rearm_epoll();

                    if (rearm_error != 0)
                    {
                        watcher_result.event_status =
                            EventStatus::None;

                        watcher_result.error =
                            rearm_error;

                        return watcher_result;
                    }

                    continue;
                }

                watcher_result.event_status =
                    EventStatus::None;

                watcher_result.error = read_error;
                return watcher_result;
            }

            if (bytes_read == 0)
            {
                watcher_result.event_status =
                    EventStatus::None;

                watcher_result.error = EIO;
                return watcher_result;
            }

            /*
             * I/O cũ đã hoàn tất.
             * read() đã trả về toàn bộ buffer hiện tại.
             */

            const std::uint32_t rearm_error =
                rearm_epoll();

            if (rearm_error != 0)
            {
                watcher_result.event_status =
                    EventStatus::None;

                watcher_result.error =
                    rearm_error;

                return watcher_result;
            }

            /*
             * Chỉ sau khi EPOLLONESHOT đã được re-arm
             * mới bắt đầu parse buffer cũ.
             */

            events_temp.resize(
                static_cast<std::size_t>(bytes_read)
            );

            const std::vector<std::byte>& event_buffer =
                events_temp;

            constexpr std::size_t event_header_size =
                offsetof(inotify_event, name);

            std::size_t event_offset = 0;

            while (event_offset < event_buffer.size())
            {
                const std::size_t bytes_remaining =
                    event_buffer.size() -
                    event_offset;

                if (bytes_remaining < event_header_size)
                {
                    watcher_result.event_status =
                        EventStatus::None;

                    watcher_result.error = EIO;
                    return watcher_result;
                }

                inotify_event event_header{};

                std::memcpy(
                    &event_header,
                    event_buffer.data() +
                        event_offset,
                    event_header_size
                );

                const std::size_t event_name_bytes =
                    static_cast<std::size_t>(
                        event_header.len
                    );

                if (
                    event_name_bytes >
                    bytes_remaining -
                    event_header_size
                )
                {
                    watcher_result.event_status =
                        EventStatus::None;

                    watcher_result.error = EIO;
                    return watcher_result;
                }

                const std::size_t event_size =
                    event_header_size +
                    event_name_bytes;

                if (
                    (event_header.mask & IN_Q_OVERFLOW) != 0
                )
                {
                    queue_overflow = true;
                }

                bool watched_directory = false;

                for (const int watch_descriptor : watch_descriptors)
                {
                    if (watch_descriptor == event_header.wd)
                    {
                        watched_directory = true;
                        break;
                    }
                }

                if (
                    (event_header.mask & IN_IGNORED) != 0 &&
                    watched_directory
                )
                {
                    watcher_result.event_status =
                        EventStatus::None;

                    watcher_result.error = ENOENT;
                    return watcher_result;
                }

                if (watched_directory && event_name_bytes != 0)
                {
                    const char* event_name_data =
                        reinterpret_cast<const char*>(
                            event_buffer.data() +
                            event_offset +
                            event_header_size
                        );

                    const void* null_character =
                        std::memchr(
                            event_name_data,
                            '\0',
                            event_name_bytes
                        );

                    const std::size_t event_name_length =
                        null_character == nullptr
                            ? event_name_bytes
                            : static_cast<std::size_t>(
                                static_cast<const char*>(
                                    null_character
                                ) - event_name_data
                            );

                    const std::string_view event_name(
                        event_name_data,
                        event_name_length
                    );

                    for (
                        std::size_t index = 0;
                        index < watch_descriptors.size();
                        ++index
                    )
                    {
                        if (
                            watch_descriptors[index] ==
                                event_header.wd &&
                            event_name == watched_file_names[index]
                        )
                        {
                            if (publish_file_changed(state))
                                target_file_changed = true;
                            break;
                        }
                    }
                }

                event_offset += event_size;
            }

            if (target_file_changed)
                return finish_after_timeout();

            if (cancellation_index >= 0)
            {
                consume_cancellation_event();
                watcher_result.error = 0;
                return finish_after_timeout();
            }

            if (stop_requested(state))
            {
                watcher_result.error = 0;
                return finish_after_timeout();
            }
        }
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
