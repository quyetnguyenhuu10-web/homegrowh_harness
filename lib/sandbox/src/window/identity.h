#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <registry.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace sandbox::detail::filesystem::windows
{
    inline constexpr std::wstring_view capability_signature = L"HomegrowphHarness";

    struct capability_identity
    {
        std::wstring name;
        std::wstring sid_string;
        std::vector<unsigned char> sid_bytes;

        [[nodiscard]] PSID sid() noexcept
        {
            return sid_bytes.empty() ? nullptr : sid_bytes.data();
        }
    };

    std::filesystem::path canonical_existing_path(
        const std::filesystem::path& input);

    std::string_view permission_name(permission value);

    capability_identity derive_capability_identity(
        const std::filesystem::path& canonical_path,
        permission access);
}
