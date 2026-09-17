#include "temp_writer.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <unistd.h>

namespace fsystem::linux::detail
{
    namespace
    {
        bool write_bytes(
            int file_descriptor,
            std::string_view data,
            fsystem::WatcherState& watcher_state,
            std::uint32_t& error
        )
        {
            std::size_t position = 0;

            while (position < data.size())
            {
                if (cancellation_requested(watcher_state))
                    return false;

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

                if (cancellation_requested(watcher_state))
                    return false;
            }

            return true;
        }
    }

    bool temporary_file::create(
        const std::filesystem::path& source_path,
        std::uint32_t& error
    )
    {
        discard();

        const std::filesystem::path temp_directory =
            source_path.parent_path().empty()
                ? std::filesystem::path(".")
                : source_path.parent_path();

        std::string temp_file_template = (
            temp_directory /
            (source_path.filename().string() + ".edit-XXXXXX")
        ).string();

        const int raw_fd = ::mkstemp(temp_file_template.data());

        if (raw_fd == -1)
        {
            error = static_cast<std::uint32_t>(errno);
            return false;
        }

        path_ = std::filesystem::path(temp_file_template);
        handle_.reset(fd_handle{raw_fd});
        return true;
    }

    void temporary_file::discard() noexcept
    {
        handle_.reset();

        if (!path_.empty())
            (void)::unlink(path_.c_str());

        path_.clear();
    }

    void temporary_file::commit_success() noexcept
    {
        handle_.reset();
        path_.clear();
    }

    bool prepare_temp_file(
        const std::filesystem::path& path,
        source_file& source,
        std::uint64_t first_occurrence,
        std::size_t old_data_size,
        const std::string& new_data,
        fsystem::WatcherState& watcher_state,
        temporary_file& temp,
        std::uint32_t& error
    )
    {
        if (!temp.create(path, error))
            return false;

        const auto fail =
            [&](std::uint32_t failure) noexcept -> bool
        {
            error = failure;
            temp.discard();
            return false;
        };

        const auto cancel = [&]() noexcept -> bool
        {
            temp.discard();
            return false;
        };

        const std::uint64_t replacement_end =
            first_occurrence + old_data_size;
        bool replacement_written = false;
        bool callback_failed = false;

        const auto write_source_chunk =
            [&](std::uint64_t offset, std::string_view chunk)
            {
                if (cancellation_requested(watcher_state))
                {
                    callback_failed = true;
                    return false;
                }

                const std::uint64_t chunk_end =
                    offset + chunk.size();

                if (!replacement_written)
                {
                    if (offset < first_occurrence)
                    {
                        const std::uint64_t prefix_end =
                            std::min(first_occurrence, chunk_end);

                        if (!write_bytes(
                                temp.get(),
                                chunk.substr(
                                    0,
                                    static_cast<std::size_t>(
                                        prefix_end - offset
                                    )
                                ),
                                watcher_state,
                                error
                            ))
                        {
                            callback_failed = true;
                            return false;
                        }
                    }

                    if (chunk_end >= first_occurrence)
                    {
                        if (cancellation_requested(watcher_state))
                        {
                            callback_failed = true;
                            return false;
                        }

                        if (!write_bytes(
                                temp.get(),
                                new_data,
                                watcher_state,
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
                                    temp.get(),
                                    chunk.substr(
                                        static_cast<std::size_t>(
                                            suffix_start - offset
                                        )
                                    ),
                                    watcher_state,
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
                            temp.get(),
                            chunk.substr(
                                static_cast<std::size_t>(
                                    suffix_start - offset
                                )
                            ),
                            watcher_state,
                            error
                        ))
                    {
                        callback_failed = true;
                        return false;
                    }
                }

                return true;
            };

        if (!source.for_each_chunk(
                write_source_chunk,
                watcher_state,
                error
            ))
        {
            if (cancellation_requested(watcher_state))
                return cancel();

            return fail(error);
        }

        if (
            source.size() == 0 &&
            first_occurrence == 0 &&
            old_data_size == 0
        )
        {
            if (cancellation_requested(watcher_state))
                return cancel();

            if (!write_bytes(
                    temp.get(),
                    new_data,
                    watcher_state,
                    error
                ))
            {
                if (cancellation_requested(watcher_state))
                    return cancel();

                return fail(error);
            }

            replacement_written = true;
        }

        if (callback_failed || !replacement_written)
        {
            if (cancellation_requested(watcher_state))
                return cancel();

            return fail(
                callback_failed
                    ? error
                    : EINVAL
            );
        }

        if (cancellation_requested(watcher_state))
            return cancel();

        if (::fsync(temp.get()) < 0)
            return fail(static_cast<std::uint32_t>(errno));

        if (cancellation_requested(watcher_state))
            return cancel();

        /* The final file-changed guard belongs to commit_replace(). */
        return true;
    }
}
