#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace sandbox
{
    enum class permission
    {
        read_only,
        read_modify,
    };

    struct registry_request
    {
        std::filesystem::path path;
        permission access = permission::read_only;
    };

    struct registered_permission
    {
        std::filesystem::path path;
        permission access = permission::read_only;
        std::wstring capability_name;
        std::wstring sid;
        bool reused = false;
    };

    struct registry_path_error
    {
        std::filesystem::path path;
        std::error_code error;
    };

    struct registry_result
    {
        std::vector<registered_permission> permissions;

        // One registry-wide OS failure that is not attributable to one path,
        // e.g. state/lock/final platform initialization or commit failure.
        std::error_code final_error;

        // Every OS/path-specific abnormality discovered while processing the
        // requested filesystem tree. Filesystem reports these paths only; it
        // does not follow or repair unsupported link/hard-link paths.
        std::vector<registry_path_error> path_errors;
    };

    /**
     * Register durable filesystem capabilities for the supplied paths.
     *
     * Each canonical path + permission pair maps to a deterministic platform
     * identity. refresh=true explicitly refreshes platform registration state
     * (and on Windows reconciles the ACL tree). refresh=false only reuses an
     * existing registration and never repairs host permissions. Later access
     * failures are therefore left to surface their original operating-system
     * errors.
     */
    registry_result registry(
        const std::vector<registry_request>& requests,
        bool refresh);
}
