#pragma once

#include "fsysteam/watcher/watcher.h"

namespace fsystem::linux
{
    WatcherResult watcher_file(
        std::filesystem::path path,
        int timeout_f
    );
}
