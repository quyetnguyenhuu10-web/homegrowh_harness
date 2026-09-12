#pragma once

#include <filesystem>
#include <string>
#include <cstdint>

namespace file
{
    struct ReadResult
    {
        std::string content;
        std::uint32_t error;
    };

    ReadResult read(std::filesystem::path path);
}
