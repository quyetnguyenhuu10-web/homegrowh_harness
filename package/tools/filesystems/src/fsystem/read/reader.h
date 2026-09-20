#pragma once

#include <filesystem>
#include <string>
#include <cstdint>
#include <vector>

namespace fsystem
{
    struct ReadRequest
    {
        std::filesystem::path path;
        /* 1-based, inclusive. */
        std::uint64_t start_line = 1;
        /* 1-based, inclusive. */
        std::uint64_t end_line = 1;
    };

    using ReadRequests = std::vector<ReadRequest>;

    struct ReadResult
    {
        std::filesystem::path path;
        /* Echoes the requested range. */
        std::uint64_t start_line = 1;
        std::uint64_t end_line = 1;
        std::string content;
        std::uint32_t error = 0;
    };

    using ReadResults = std::vector<ReadResult>;

    ReadResults read(const ReadRequests& requests);

    ReadResult read(
        const std::filesystem::path& path,
        std::uint64_t start_line,
        std::uint64_t end_line
    );

    ReadResult read(const std::filesystem::path& path);
}
