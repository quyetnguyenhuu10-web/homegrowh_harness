#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fsystem
{
    struct WriteRequest
    {
        std::filesystem::path path;
        std::string new_content;
    };

    using WriteRequests = std::vector<WriteRequest>;

    struct WriteResult
    {
        std::filesystem::path path;
        std::uint32_t error = 0;
    };

    using WriteResults = std::vector<WriteResult>;

    WriteResults write(const WriteRequests& requests);

    WriteResult write(
        const std::filesystem::path& path,
        const std::string& new_content
    );
}
