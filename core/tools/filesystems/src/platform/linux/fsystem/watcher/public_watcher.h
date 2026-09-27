#pragma once

#include <fsystem/watcher/watcher.h>

#include <filesystem>

namespace fsystem::linux
{
    WatcherResult watcher_file(
        const std::filesystem::path& path,
        int timeout_f
    );
}
