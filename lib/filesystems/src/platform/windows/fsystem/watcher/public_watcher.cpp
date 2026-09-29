#include "public_watcher.h"

#include "windows_watcher.h"

namespace fsystem::windows
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
