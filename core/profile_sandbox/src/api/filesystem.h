#pragma once

#include "registry.h"

#include <vector>

namespace sandbox::detail::filesystem
{
    registry_result refresh_permissions(
        const std::vector<registry_request>& requests);

    registry_result reuse_permissions(
        const std::vector<registry_request>& requests);

    release_result release_permissions(const std::filesystem::path& path);

    release_result release_all_permissions();
}
