#pragma once

#include "fsystem/watcher/watcher.h"

namespace fsystem::windows
{
    WatcherResult watcher_file(
        std::filesystem::path path,
        int timeout_f
    );
}
