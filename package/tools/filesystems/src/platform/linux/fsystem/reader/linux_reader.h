#pragma once

#include "fsystem/read/reader.h"

namespace fsystem::linux
{
    ReadResults read_file(const ReadRequests& requests);

    ReadResult read_file(
        const std::filesystem::path& path,
        std::uint64_t start_line,
        std::uint64_t end_line
    );

    ReadResult read_file(const std::filesystem::path& path);
}
