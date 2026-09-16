#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace test_support
{
    struct temporary_file_guard
    {
        std::filesystem::path path;

        ~temporary_file_guard() noexcept
        {
            std::error_code error;
            (void)std::filesystem::remove(path, error);
        }
    };

    inline bool write_file(
        const std::filesystem::path& path,
        std::string_view content
    )
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);

        if (!output)
            return false;

        output.write(
            content.data(),
            static_cast<std::streamsize>(content.size())
        );

        return output.good();
    }
}
