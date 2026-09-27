#include "filesystem.h"

#if defined(_WIN32)

#include "../windows/filesystem.h"

#elif defined(__linux__)

#include "../linux/filesystem.h"

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

    release_result release_permissions(const std::filesystem::path& path)
    {
#if defined(_WIN32)
        return windows::release_permissions(path);
#elif defined(__linux__)
        return linux::release_permissions(path);
#endif
    }

    release_result release_all_permissions()
    {
#if defined(_WIN32)
        return windows::release_all_permissions();
#elif defined(__linux__)
        return linux::release_all_permissions();
#endif
    }
}
