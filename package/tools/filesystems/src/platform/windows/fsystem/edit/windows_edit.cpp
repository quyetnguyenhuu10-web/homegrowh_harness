#include "window_edit.h"

#include <config/edit_config.h>
#include <fsystem/edit/edit_detail.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

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

        constexpr std::size_t stream_chunk_capacity =
            fsystem::config::edit_stream_chunk_capacity;

        bool write_bytes(
            HANDLE file_handle,
            std::string_view data,
            std::uint32_t& error
        )
        {
            std::size_t position = 0;

            while (position < data.size())
            {
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

                HANDLE raw_handle = CreateFileW(
                    path.c_str(),
                    GENERIC_READ,
                    FILE_SHARE_READ |
                    FILE_SHARE_WRITE |
                    FILE_SHARE_DELETE,
                    nullptr,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL |
                    FILE_FLAG_SEQUENTIAL_SCAN,
                    nullptr
                );

                if (raw_handle == INVALID_HANDLE_VALUE)
                {
                    error = GetLastError();
                    return false;
                }

                handle_.reset(raw_handle);

                LARGE_INTEGER file_size{};

                if (!GetFileSizeEx(handle_.get(), &file_size))
                {
                    error = GetLastError();
                    handle_.reset();
                    return false;
                }

                if (file_size.QuadPart < 0)
                {
                    error = ERROR_INVALID_DATA;
                    handle_.reset();
                    return false;
                }

                size_ = static_cast<std::uint64_t>(
                    file_size.QuadPart
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
                LARGE_INTEGER origin{};

                if (!SetFilePointerEx(
                        handle_.get(),
                        origin,
                        nullptr,
                        FILE_BEGIN
                    ))
                {
                    error = GetLastError();
                    return false;
                }

                std::string chunk(stream_chunk_capacity, '\0');
                std::uint64_t position = 0;

                while (position < size_)
                {
                    const std::uint64_t remaining = size_ - position;
                    const DWORD requested = static_cast<DWORD>(
                        std::min(
                            remaining,
                            static_cast<std::uint64_t>(
                                stream_chunk_capacity
                            )
                        )
                    );

                    DWORD filled = 0;

                    while (filled < requested)
                    {
                        DWORD bytes_read = 0;

                        if (!ReadFile(
                                handle_.get(),
                                chunk.data() + filled,
                                requested - filled,
                                &bytes_read,
                                nullptr
                            ))
                        {
                            error = GetLastError();
                            return false;
                        }

                        if (bytes_read == 0)
                        {
                            error = ERROR_HANDLE_EOF;
                            return false;
                        }

                        filled += bytes_read;
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
            unique_handle handle_{nullptr};
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
                    ? std::filesystem::path(L".")
                    : path.parent_path();

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

            const std::filesystem::path temp_path(temp_file_name);

            HANDLE raw_handle = CreateFileW(
                temp_path.c_str(),
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
                (void)DeleteFileW(temp_path.c_str());
                return false;
            }

            unique_handle handle(raw_handle);

            const auto fail = [&](std::uint32_t failure) noexcept
                -> bool
            {
                error = failure;
                handle.reset();
                (void)DeleteFileW(temp_path.c_str());
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
                                    handle.get(),
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
                                    handle.get(),
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
                                        handle.get(),
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
                                handle.get(),
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
                if (!write_bytes(handle.get(), new_data, error))
                    return fail(error);

                replacement_written = true;
            }

            if (callback_failed || !replacement_written)
            {
                return fail(
                    callback_failed
                        ? error
                        : ERROR_INVALID_DATA
                );
            }

            if (!FlushFileBuffers(handle.get()))
                return fail(GetLastError());

            if (
                watcher_state.file_changed.load(
                    std::memory_order_acquire
                )
            )
            {
                handle.reset();
                (void)DeleteFileW(temp_path.c_str());
                return false;
            }

            handle.reset();

            /* The source must be closed before the final replacement. */
            source.reset();
            replace_attempted = true;

            if (!ReplaceFileW(
                    path.c_str(),
                    temp_path.c_str(),
                    nullptr,
                    REPLACEFILE_WRITE_THROUGH,
                    nullptr,
                    nullptr
                ))
            {
                error = GetLastError();
                (void)DeleteFileW(temp_path.c_str());
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
            edit_result.error = ERROR_INVALID_DATA;
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
