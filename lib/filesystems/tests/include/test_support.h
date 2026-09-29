#pragma once

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
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

    inline std::string read_file(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return {};

        return {
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
    }
}

// Test-only compatibility for legacy assertions inside edit_test.cpp.
// Production <fsystem> exposes only edit + watcher.
namespace fsystem
{
    struct ReadResult
    {
        std::string content;
        int error = 0;
    };

    inline ReadResult read(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
            return {{}, 1};

        std::string content{
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        };
        return {content, input.bad() ? 1 : 0};
    }
}
