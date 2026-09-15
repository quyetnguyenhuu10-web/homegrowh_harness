#include "linux_edit.h"

#include <fsystem/edit/edit_detail.h>
#include <fsystem/read/reader.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <limits>
#include <memory>
#include <string>
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

        std::string build_new_content(
            const std::string& content,
            std::size_t first_occurrence,
            const std::string& old_data,
            const std::string& new_data
        )
        {
            std::string block_before =
                content.substr(0, first_occurrence);

            std::string block_old_data =
                content.substr(first_occurrence, old_data.size());

            std::string block_after =
                content.substr(first_occurrence + old_data.size());

            std::string new_content =
                block_before + new_data + block_after;

            block_before.clear();
            block_old_data.clear();
            block_after.clear();

            return new_content;
        }

        bool write_and_replace(
            const std::filesystem::path& path,
            const std::string& new_content,
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

            const std::filesystem::path temp_path(temp_file_template);
            unique_fd handle{fd_handle{raw_fd}};
            const int file_descriptor = handle.get().get();

            std::size_t position = 0;

            while (position < new_content.size())
            {
                const std::size_t remaining = new_content.size() - position;
                const std::size_t bytes_to_write =
                    remaining > static_cast<std::size_t>(
                        std::numeric_limits<ssize_t>::max()
                    )
                        ? static_cast<std::size_t>(
                            std::numeric_limits<ssize_t>::max()
                        )
                        : remaining;

                const ssize_t bytes_written = ::write(
                    file_descriptor,
                    new_content.data() + position,
                    bytes_to_write
                );

                if (bytes_written < 0)
                {
                    if (errno == EINTR)
                        continue;

                    const std::uint32_t write_error =
                        static_cast<std::uint32_t>(errno);
                    handle.reset();
                    (void)::unlink(temp_path.c_str());
                    error = write_error;
                    return false;
                }

                if (bytes_written == 0)
                {
                    handle.reset();
                    (void)::unlink(temp_path.c_str());
                    error = EIO;
                    return false;
                }

                position += static_cast<std::size_t>(bytes_written);
            }

            if (::fsync(file_descriptor) < 0)
            {
                const std::uint32_t sync_error =
                    static_cast<std::uint32_t>(errno);
                handle.reset();
                (void)::unlink(temp_path.c_str());
                error = sync_error;
                return false;
            }

            handle.reset();

            if (::rename(temp_path.c_str(), path.c_str()) < 0)
            {
                const std::uint32_t rename_error =
                    static_cast<std::uint32_t>(errno);
                (void)::unlink(temp_path.c_str());
                error = rename_error;
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

        auto result = fsystem::read(path);

        if (result.error != 0)
        {
            edit_result.error = result.error;
            return edit_result;
        }

        edit_result.old_content = result.content;

        std::size_t first_occurrence = std::string::npos;

        edit_result.note = fsystem::detail::find_old_data(
            result.content,
            old_data,
            first_occurrence
        );

        if (edit_result.note != EditNote::none)
            return edit_result;

        edit_result.new_content = build_new_content(
            result.content,
            first_occurrence,
            old_data,
            new_data
        );

        /*
         * Đây là điểm kiểm tra cuối cùng. Các bước đọc, tìm và dựng nội
         * dung mới vẫn hoàn tất trước khi hỏi watcher về cạnh tranh file.
         */
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

        if (
            watcher_state.file_changed.load(std::memory_order_acquire) ||
            watcher_result.event_status == EventStatus::HasEvent
        )
        {
            edit_result.note = EditNote::file_changed;
            return edit_result;
        }

        write_and_replace(
            path,
            edit_result.new_content,
            edit_result.error
        );

        return edit_result;
    }
}
