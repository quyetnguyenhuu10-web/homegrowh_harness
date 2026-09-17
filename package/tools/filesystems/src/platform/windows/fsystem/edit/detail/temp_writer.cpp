#include "temp_writer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fsystem::windows::detail
{
    namespace
    {
        bool write_bytes(
            HANDLE file_handle,
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

                const std::size_t remaining = data.size() - position;

                const DWORD bytes_to_write = static_cast<DWORD>(
                    std::min(
                        remaining,
                        static_cast<std::size_t>(MAXDWORD)
                    )
                );

                DWORD bytes_written = 0;

                if (!WriteFile(
                        file_handle,
                        data.data() + position,
                        bytes_to_write,
                        &bytes_written,
                        nullptr
                    ))
                {
                    error = GetLastError();
                    return false;
                }

                if (
                    bytes_written == 0 ||
                    bytes_written > bytes_to_write
                )
                {
                    error = ERROR_WRITE_FAULT;
                    return false;
                }

                position += bytes_written;

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
                ? std::filesystem::path(L".")
                : source_path.parent_path();

        wchar_t temp_file_name[MAX_PATH]{};

        if (GetTempFileNameW(
                temp_directory.c_str(),
                L"edt",
                0,
                temp_file_name
            ) == 0)
        {
            error = GetLastError();
            return false;
        }

        path_ = std::filesystem::path(temp_file_name);

        HANDLE raw_handle = CreateFileW(
            path_.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (raw_handle == INVALID_HANDLE_VALUE)
        {
            error = GetLastError();
            discard();
            return false;
        }

        handle_.reset(raw_handle);
        return true;
    }

    void temporary_file::discard() noexcept
    {
        handle_.reset();

        if (!path_.empty())
            (void)DeleteFileW(path_.c_str());

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

        const auto fail = [&](std::uint32_t failure) noexcept
            -> bool
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
                    : ERROR_INVALID_DATA
            );
        }

        if (cancellation_requested(watcher_state))
            return cancel();

        if (!FlushFileBuffers(temp.get()))
            return fail(GetLastError());

        if (cancellation_requested(watcher_state))
            return cancel();

        /* The final file-changed guard belongs to commit_replace(). */
        return true;
    }
}
