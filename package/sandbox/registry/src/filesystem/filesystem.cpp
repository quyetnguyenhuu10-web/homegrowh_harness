#include "filesystem.h"

#if defined(_WIN32)

#include "../platform/windows/filesystem/windows_filesystem.h"

#elif defined(__linux__)

#include "../platform/linux/filesystem/linux_filesystem.h"

#else

#error "Unsupported operating system"

#endif

namespace sandbox::detail::filesystem
{
    registry_result refresh_permissions(
        const std::vector<registry_request>& requests)
    {
#if defined(_WIN32)
        return windows::refresh_permissions(requests);
#elif defined(__linux__)
        return linux::refresh_permissions(requests);
#endif
    }

    registry_result reuse_permissions(
        const std::vector<registry_request>& requests)
    {
#if defined(_WIN32)
        return windows::reuse_permissions(requests);
#elif defined(__linux__)
        return linux::reuse_permissions(requests);
#endif
    }
}
