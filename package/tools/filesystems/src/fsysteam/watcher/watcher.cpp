#include "fsysteam/watcher/watcher.h"

#if defined(_WIN32)

#include "../../platform/windows/fsystem/watcher/windows_watcher.h"

namespace fsystem
{
    WatcherResult watcher(
        std::filesystem::path path,
        std::uint32_t timeout
    )
    {
        return windows::watcher_file(path, timeout);
    }
}

#elif defined(__linux__)

namespace fsystem
{
    WatcherResult watcher(std::filesystem::path, std::uint32_t)
    {
        // Linux implementation is not available yet.
        return {};
    }
}

#else

#error "Unsupported operating system"

#endif
