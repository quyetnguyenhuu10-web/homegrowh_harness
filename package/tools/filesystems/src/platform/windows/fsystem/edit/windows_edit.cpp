#include "window_edit.h"

#include <fsysteam/read/reader.h>
#include <Windows.h>

#include <memory>

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

        EditNote find_old_data(
            const std::string& content,
            const std::string& old_data,
            std::size_t& first_occurrence
        )
        {
            first_occurrence = content.find(old_data);

            if (first_occurrence == std::string::npos)
                return EditNote::old_data_not_found;

            const std::size_t second_occurrence =
                content.find(old_data, first_occurrence + 1);

            if (second_occurrence != std::string::npos)
                return EditNote::old_data_appears_more_than_once;

            return EditNote::none;
        }

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
            HANDLE hFile = handle.get();

            std::size_t position = 0;

            while (position < new_content.size())
            {
                const std::size_t remaining = new_content.size() - position;

                const DWORD bytes_to_write =
                    remaining > static_cast<std::size_t>(MAXDWORD)
                        ? MAXDWORD
                        : static_cast<DWORD>(remaining);

                DWORD bytes_written = 0;

                if (!WriteFile(
                        hFile,
                        new_content.data() + position,
                        bytes_to_write,
                        &bytes_written,
                        nullptr
                    ))
                {
                    error = GetLastError();
                    handle.reset();
                    (void)DeleteFileW(temp_path.c_str());
                    return false;
                }

                if (bytes_written == 0 || bytes_written > bytes_to_write)
                {
                    handle.reset();
                    (void)DeleteFileW(temp_path.c_str());
                    error = ERROR_WRITE_FAULT;
                    return false;
                }

                position += bytes_written;
            }

            if (!FlushFileBuffers(hFile))
            {
                error = GetLastError();
                handle.reset();
                (void)DeleteFileW(temp_path.c_str());
                return false;
            }

            handle.reset();

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

        auto result = fsystem::read(path);

        if (result.error != 0)
        {
            edit_result.error = result.error;
            return edit_result;
        }

        edit_result.old_content = result.content;

        std::size_t first_occurrence = std::string::npos;

        edit_result.note = find_old_data(
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

        write_and_replace(
            path,
            edit_result.new_content,
            edit_result.error
        );

        return edit_result;
    }
}
