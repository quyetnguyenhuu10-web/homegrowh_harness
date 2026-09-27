#include "public_watcher.h"

#include "linux_watcher.h"

namespace fsystem::linux
{
    WatcherResult watcher_file(
        const std::filesystem::path& path,
        int timeout_f
    )
    {
        return watcher_common::watcher_file(
            path,
            timeout_f,
            nullptr
        );
    }
}
