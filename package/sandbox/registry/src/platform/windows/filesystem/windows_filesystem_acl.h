#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <sandbox/registry.h>

#include <filesystem>
#include <optional>
#include <vector>

namespace sandbox::detail::filesystem::windows
{
    bool inspect_registry_path(
        const std::filesystem::path& path,
        std::vector<registry_path_error>& path_errors);

    bool compatible_acl(
        const std::filesystem::path& path,
        PSID sid,
        permission access,
        bool directory);

    bool reconcile_tree(
        const std::filesystem::path& root,
        PSID sid,
        permission access,
        std::vector<registry_path_error>& path_errors);

    std::optional<registry_path_error> release_tree(
        const std::filesystem::path& root,
        const std::wstring& sid);
}
