#include "fsysteam/watcher/watcher.h"

#if defined(_WIN32)

#include "../../platform/windows/fsystem/watcher/windows_watcher.h"

namespace fsystem
{
    WatcherResult watcher(
        std::filesystem::path path,
        int timeout_f
    )
    {
        return windows::watcher_file(path, timeout_f);
    }
}

#elif defined(__linux__)

#include "../../platform/linux/fsysteam/watcher/linux_watcher.h"

namespace fsystem
{
    WatcherResult watcher(
        std::filesystem::path path,
        int timeout_f
    )
    {
        return linux::watcher_file(path, timeout_f);
    }
}

#else

#error "Unsupported operating system"

#endif
