#pragma once

#include "temp_writer.h"

#include <cstdint>
#include <filesystem>

namespace fsystem::linux::detail
{
    bool commit_replace(
        const std::filesystem::path& path,
        source_file& source,
        temporary_file& temp,
        fsystem::WatcherState& watcher_state,
        bool& replace_attempted,
        std::uint32_t& error
    );
}
