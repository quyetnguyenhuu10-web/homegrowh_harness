#pragma once

#include "source_stream.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace fsystem::windows::detail
{
    class temporary_file
    {
    public:
        temporary_file() noexcept = default;
        ~temporary_file() noexcept
        {
            discard();
        }

        temporary_file(const temporary_file&) = delete;
        temporary_file& operator=(const temporary_file&) = delete;

        bool create(
            const std::filesystem::path& source_path,
            std::uint32_t& error
        );

        HANDLE get() const noexcept
        {
            return handle_.get();
        }

        const std::filesystem::path& file_path() const noexcept
        {
            return path_;
        }

        void close() noexcept
        {
            handle_.reset();
        }

        void discard() noexcept;

        void commit_success() noexcept;

    private:
        unique_handle handle_{nullptr};
        std::filesystem::path path_;
    };

    bool prepare_temp_file(
        const std::filesystem::path& path,
        source_file& source,
        std::uint64_t first_occurrence,
        std::size_t old_data_size,
        const std::string& new_data,
        watcher_state& watcher_state,
        temporary_file& temp,
        std::uint32_t& error
    );
}
