#pragma once

#include "fsystem/write/writer.h"

namespace fsystem::linux
{
    WriteResults write_file(const WriteRequests& requests);

    WriteResult write_file(
        const std::filesystem::path& path,
        const std::string& new_content
    );
}
