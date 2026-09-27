#include "../executable/process_path.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <filesystem>
#include <string>

namespace sandbox::executable::detail
{
    std::filesystem::path path_from_utf8(const std::string& value)
    {
        if (value.empty())
            return {};

        const int required = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (required <= 0)
            return {};

        std::wstring wide(static_cast<std::size_t>(required), L'\0');
        if (MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                wide.data(),
                required) <= 0)
        {
            return {};
        }
        return std::filesystem::path(wide);
    }

    std::string path_to_utf8(const std::filesystem::path& path)
    {
        const std::wstring wide = path.native();
        if (wide.empty())
            return {};

        const int required = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            wide.data(),
            static_cast<int>(wide.size()),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (required <= 0)
            return {};

        std::string output(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                wide.data(),
                static_cast<int>(wide.size()),
                output.data(),
                required,
                nullptr,
                nullptr) <= 0)
        {
            return {};
        }
        return output;
    }
}
