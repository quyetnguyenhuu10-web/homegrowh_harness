#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include "../error_schema.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace sandbox::detail::filesystem::windows
{
    [[noreturn]] inline void throw_win32(
        std::string_view action,
        DWORD error,
        const std::optional<std::filesystem::path>& path = std::nullopt)
    {
        sandbox::detail::throw_error(
            sandbox::detail::make_native_error(
                std::string(action),
                    error,
                path));
    }
}
