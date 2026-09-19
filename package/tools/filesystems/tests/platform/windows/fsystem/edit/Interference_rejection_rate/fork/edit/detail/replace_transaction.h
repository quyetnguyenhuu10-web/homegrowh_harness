#pragma once

#include "temp_writer.h"

#include <cstdint>
#include <filesystem>

namespace fsystem::windows_irr::detail
{
    bool commit_replace(
        const std::filesystem::path& path,
        source_file& source,
        temporary_file& temp,
        watcher_state& watcher_state,
        bool& replace_attempted,
        std::uint32_t& error
    );
}
