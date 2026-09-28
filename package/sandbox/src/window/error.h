#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace sandbox::detail::filesystem::windows
{
    [[noreturn]] inline void throw_win32(std::string_view action, DWORD error)
    {
        throw std::system_error(
            static_cast<int>(error),
            std::system_category(),
            std::string(action));
    }
}
