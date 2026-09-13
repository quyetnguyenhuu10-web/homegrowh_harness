#pragma once

#include "fsysteam/watcher/watcher.h"

namespace fsystem::windows
{
    WatcherResult watcher_file(
        std::filesystem::path path,
        std::uint32_t timeout
    );
}
