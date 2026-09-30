#include "platform.h"

#include "../error/error.h"

#include <cerrno>
#include <cstdlib>
#include <limits>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

namespace tool_runtime::detail
{
    Result<std::filesystem::path::string_type> environment_text(std::string_view name)
    {
        using text_type = std::filesystem::path::string_type;
        try
        {
            if (name.find('\0') != std::string_view::npos)
                return Result<text_type>::failure(make_error("read_environment", "validation_error",
                    "Environment variable name contains a null character", {{"environment_variable", text_payload(name)}}));
#if defined(_WIN32)
            if (name.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                return Result<text_type>::failure(make_error("read_environment", "validation_error",
                    "Environment variable name exceeds the conversion API size limit", {{"size", name.size()}}));
            std::wstring native_name;
            if (!name.empty())
            {
                const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                    name.data(), static_cast<int>(name.size()), nullptr, 0);
                if (size <= 0)
                {
                    const DWORD code = GetLastError();
                    return Result<text_type>::failure(make_system_error("read_environment", "MultiByteToWideChar",
                        {static_cast<int>(code), std::system_category()}, {{"environment_variable", text_payload(name)}}));
                }
                native_name.resize(static_cast<std::size_t>(size));
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()),
                        native_name.data(), size) != size)
                {
                    const DWORD code = GetLastError();
                    return Result<text_type>::failure(make_system_error("read_environment", "MultiByteToWideChar",
                        {static_cast<int>(code), std::system_category()}, {{"environment_variable", text_payload(name)}}));
                }
            }
            errno = 0;
            const wchar_t* value = ::_wgetenv(native_name.c_str());
            const int code = errno;
            constexpr std::string_view api = "_wgetenv";
#else
            const std::string native_name(name);
            errno = 0;
            const char* value = std::getenv(native_name.c_str());
            const int code = errno;
            constexpr std::string_view api = "std::getenv";
#endif
            if (value != nullptr)
                return Result<text_type>::success(text_type(value));
            if (code != 0)
                return Result<text_type>::failure(make_system_error("read_environment", api,
                    {code, std::generic_category()}, {{"environment_variable", text_payload(name)}}));
            return Result<text_type>::success(text_type{});
        }
        catch (...)
        {
            Error error = current_exception_error("read_environment");
            error.data.push_back({{"environment_variable", text_payload(name)}});
            return Result<text_type>::failure(std::move(error));
        }
    }
}
