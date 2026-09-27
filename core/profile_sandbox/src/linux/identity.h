#pragma once

#include "../api/registry.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace sandbox::detail::filesystem::linux
{
    inline constexpr std::string_view capability_signature = "HomegrowphHarness";

    std::filesystem::path canonical_existing_path(
        const std::filesystem::path& input);

    std::string_view permission_name(permission value);

    std::wstring policy_identity(
        const std::filesystem::path& canonical_path,
        permission access);
}
