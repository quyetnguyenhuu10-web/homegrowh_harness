#pragma once

#include <sandbox/registry.h>

#include <vector>

namespace sandbox::detail::filesystem
{
    registry_result refresh_permissions(
        const std::vector<registry_request>& requests);

    registry_result reuse_permissions(
        const std::vector<registry_request>& requests);
}
