#include "fsystem/watcher/watcher.h"

#if defined(_WIN32)

#include "../../platform/windows/fsystem/watcher/windows_watcher.h"

#elif defined(__linux__)

#include "../../platform/linux/fsystem/watcher/linux_watcher.h"

#else

#error "Unsupported operating system"

#endif

namespace fsystem
{
    WatcherResult watcher(
        std::filesystem::path path,
        int timeout_f
    )
    {
#if defined(_WIN32)
        return windows::watcher_file(path, timeout_f);
#elif defined(__linux__)
        return linux::watcher_file(path, timeout_f);
#endif
    }

    WatcherResult watcher(
        std::filesystem::path path,
        int timeout_f,
        WatcherState& state
    )
    {
#if defined(_WIN32)
        return windows::watcher_file(path, timeout_f, &state);
#elif defined(__linux__)
        return linux::watcher_file(path, timeout_f, &state);
#endif
    }
}
