#include "linux_edit.h"

#include <config/edit_config.h>
#include <fsystem/edit/edit_detail.h>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fsystem::linux
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

        constexpr std::size_t stream_chunk_capacity =
            fsystem::config::edit_stream_chunk_capacity;

        bool write_bytes(
            int file_descriptor,
            std::string_view data,
            std::uint32_t& error
        )
        {
            std::size_t position = 0;

            while (position < data.size())
            {
                const std::size_t bytes_to_write = std::min(
                    data.size() - position,
                    static_cast<std::size_t>(
                        std::numeric_limits<ssize_t>::max()
                    )
                );

                const ssize_t bytes_written = ::write(
                    file_descriptor,
                    data.data() + position,
                    bytes_to_write
                );

                if (bytes_written < 0)
                {
                    if (errno == EINTR)
                        continue;

                    error = static_cast<std::uint32_t>(errno);
                    return false;
                }

                if (bytes_written == 0)
                {
                    error = EIO;
                    return false;
                }

                position += static_cast<std::size_t>(bytes_written);
            }

            return true;
        }

        class source_file
        {
        public:
            bool open(
                const std::filesystem::path& path,
                std::uint32_t& error
            )
            {
                handle_.reset();

                const int raw_fd = ::open(
                    path.c_str(),
                    O_RDONLY | O_CLOEXEC
                );

                if (raw_fd < 0)
                {
                    error = static_cast<std::uint32_t>(errno);
                    return false;
                }

                handle_.reset(fd_handle{raw_fd});

                struct stat file_status{};

                if (::fstat(handle_.get().get(), &file_status) < 0)
                {
                    error = static_cast<std::uint32_t>(errno);
                    handle_.reset();
                    return false;
                }

                if (file_status.st_size < 0)
                {
                    error = EOVERFLOW;
                    handle_.reset();
                    return false;
                }

                size_ = static_cast<std::uint64_t>(
                    file_status.st_size
                );
                return true;
            }

            void reset() noexcept
            {
                handle_.reset();
                size_ = 0;
            }

            std::uint64_t size() const noexcept
            {
                return size_;
            }

            template<typename callback_type>
            bool for_each_chunk(
                callback_type&& callback,
                std::uint32_t& error
            )
            {
                if (::lseek(handle_.get().get(), 0, SEEK_SET) < 0)
                {
                    error = static_cast<std::uint32_t>(errno);
                    return false;
                }

                std::string chunk(stream_chunk_capacity, '\0');
                std::uint64_t position = 0;

                while (position < size_)
                {
                    const std::size_t requested = static_cast<std::size_t>(
                        std::min(
                            size_ - position,
                            static_cast<std::uint64_t>(
                                stream_chunk_capacity
                            )
                        )
                    );

                    std::size_t filled = 0;

                    while (filled < requested)
                    {
                        const ssize_t bytes_read = ::read(
                            handle_.get().get(),
                            chunk.data() + filled,
                            requested - filled
                        );

                        if (bytes_read < 0)
                        {
                            if (errno == EINTR)
                                continue;

                            error = static_cast<std::uint32_t>(errno);
                            return false;
                        }

                        if (bytes_read == 0)
                        {
                            error = EIO;
                            return false;
                        }

                        filled += static_cast<std::size_t>(bytes_read);
                    }

                    if (!callback(
                            position,
                            std::string_view(chunk.data(), filled)
                        ))
                    {
                        return true;
                    }

                    position += filled;
                }

                return true;
            }

        private:
            unique_fd handle_{fd_handle{}};
            std::uint64_t size_ = 0;
        };

        bool write_and_replace(
            const std::filesystem::path& path,
            source_file& source,
            std::uint64_t first_occurrence,
            std::size_t old_data_size,
            const std::string& new_data,
            fsystem::WatcherState& watcher_state,
            bool& replace_attempted,
            std::uint32_t& error
        )
        {
            const std::filesystem::path temp_directory =
                path.parent_path().empty()
                    ? std::filesystem::path(".")
                    : path.parent_path();

            std::string temp_file_template = (
                temp_directory /
                (path.filename().string() + ".edit-XXXXXX")
            ).string();

            const int raw_fd = ::mkstemp(temp_file_template.data());

            if (raw_fd == -1)
            {
                error = static_cast<std::uint32_t>(errno);
                return false;
            }

            const std::filesystem::path temp_path(
                temp_file_template
            );

            unique_fd handle{fd_handle{raw_fd}};

            const auto fail =
                [&](std::uint32_t failure) noexcept -> bool
            {
                error = failure;
                handle.reset();
                (void)::unlink(temp_path.c_str());
                return false;
            };

            const std::uint64_t replacement_end =
                first_occurrence + old_data_size;
            bool replacement_written = false;
            bool callback_failed = false;

            const auto write_source_chunk =
                [&](std::uint64_t offset, std::string_view chunk)
                {
                    const std::uint64_t chunk_end =
                        offset + chunk.size();

                    if (!replacement_written)
                    {
                        if (offset < first_occurrence)
                        {
                            const std::uint64_t prefix_end =
                                std::min(first_occurrence, chunk_end);

                            if (!write_bytes(
                                    handle.get().get(),
                                    chunk.substr(
                                        0,
                                        static_cast<std::size_t>(
                                            prefix_end - offset
                                        )
                                    ),
                                    error
                                ))
                            {
                                callback_failed = true;
                                return false;
                            }
                        }

                        if (chunk_end >= first_occurrence)
                        {
                            if (!write_bytes(
                                    handle.get().get(),
                                    new_data,
                                    error
                                ))
                            {
                                callback_failed = true;
                                return false;
                            }

                            replacement_written = true;

                            if (chunk_end > replacement_end)
                            {
                                const std::uint64_t suffix_start =
                                    std::max(offset, replacement_end);

                                if (!write_bytes(
                                        handle.get().get(),
                                        chunk.substr(
                                            static_cast<std::size_t>(
                                                suffix_start - offset
                                            )
                                        ),
                                        error
                                    ))
                                {
                                    callback_failed = true;
                                    return false;
                                }
                            }
                        }
                    }
                    else if (chunk_end > replacement_end)
                    {
                        const std::uint64_t suffix_start =
                            std::max(offset, replacement_end);

                        if (!write_bytes(
                                handle.get().get(),
                                chunk.substr(
                                    static_cast<std::size_t>(
                                        suffix_start - offset
                                    )
                                ),
                                error
                            ))
                        {
                            callback_failed = true;
                            return false;
                        }
                    }

                    return true;
                };

            if (!source.for_each_chunk(write_source_chunk, error))
                return fail(error);

            if (
                source.size() == 0 &&
                first_occurrence == 0 &&
                old_data_size == 0
            )
            {
                if (!write_bytes(handle.get().get(), new_data, error))
                    return fail(error);

                replacement_written = true;
            }

            if (callback_failed || !replacement_written)
            {
                return fail(
                    callback_failed
                        ? error
                        : EINVAL
                );
            }

            if (::fsync(handle.get().get()) < 0)
                return fail(static_cast<std::uint32_t>(errno));

            if (
                watcher_state.file_changed.load(
                    std::memory_order_acquire
                )
            )
            {
                handle.reset();
                (void)::unlink(temp_path.c_str());
                return false;
            }

            handle.reset();

            source.reset();
            replace_attempted = true;

            if (::rename(temp_path.c_str(), path.c_str()) < 0)
            {
                error = static_cast<std::uint32_t>(errno);
                (void)::unlink(temp_path.c_str());
                return false;
            }

            return true;
        }
    }

    EditResult edit_file(
        std::filesystem::path path,
        std::string old_data,
        std::string new_data
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

        if (
            watcher_state.finished.load(
                std::memory_order_acquire
            ) &&
            !watcher_state.ready.load(
                std::memory_order_acquire
            )
        )
        {
            watcher_thread.stop();

            edit_result.error = watcher_exception != nullptr
                ? fsystem::detail::watcher_thread_exception_error
                : watcher_result.error;

            return edit_result;
        }

        source_file source;

        if (std::uint32_t error = 0;
            !source.open(path, error))
        {
            watcher_thread.stop();
            edit_result.error = error;
            return edit_result;
        }

        fsystem::detail::chunk_matcher matcher(old_data);

        if (!source.for_each_chunk(
                [&](std::uint64_t offset, std::string_view chunk)
                {
                    return matcher.consume(offset, chunk);
                },
                edit_result.error
            ))
        {
            watcher_thread.stop();
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

        edit_result.old_content = old_data;
        edit_result.new_content = new_data;

        const bool replaced = write_and_replace(
            path,
            source,
            matcher.first_occurrence(),
            old_data.size(),
            new_data,
            watcher_state,
            edit_result.replace_attempted,
            edit_result.error
        );

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
