#include "platform.h"

#include "../error/error.h"

#include <array>
#include <cerrno>
#include <fstream>
#include <limits>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace tool_runtime::detail
{
    std::string path_text(const std::filesystem::path& path)
    {
        const std::u8string text = path.u8string();
        return {reinterpret_cast<const char*>(text.data()), text.size()};
    }

    Result<std::filesystem::path> current_executable()
    {
        try
        {
#if defined(_WIN32)
            std::wstring buffer(256, L'\0');
            for (;;)
            {
                const DWORD written = GetModuleFileNameW(
                    nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (written == 0)
                {
                    const DWORD code = GetLastError();
                    return Result<std::filesystem::path>::failure(make_system_error(
                        "current_executable", "GetModuleFileNameW",
                        {static_cast<int>(code), std::system_category()}));
                }
                if (written < buffer.size())
                {
                    buffer.resize(written);
                    return Result<std::filesystem::path>::success(std::filesystem::path(std::move(buffer)));
                }
                const DWORD code = GetLastError();
                if (buffer.size() > (std::numeric_limits<DWORD>::max)() / 2)
                {
                    return Result<std::filesystem::path>::failure(make_system_error(
                        "current_executable", "GetModuleFileNameW",
                        {static_cast<int>(code), std::system_category()}, {{"buffer_size", buffer.size()}}));
                }
                buffer.resize(buffer.size() * 2);
            }
#elif defined(__linux__)
            std::string buffer(256, '\0');
            for (;;)
            {
                const ssize_t written = ::readlink("/proc/self/exe", buffer.data(), buffer.size());
                if (written < 0)
                {
                    const int code = errno;
                    return Result<std::filesystem::path>::failure(make_system_error(
                        "current_executable", "readlink", {code, std::generic_category()}, {{"path", "/proc/self/exe"}}));
                }
                if (static_cast<std::size_t>(written) < buffer.size())
                {
                    buffer.resize(static_cast<std::size_t>(written));
                    return Result<std::filesystem::path>::success(std::filesystem::path(std::move(buffer)));
                }
                buffer.resize(buffer.size() * 2);
            }
#else
#error "Unsupported operating system"
#endif
        }
        catch (...)
        {
            return Result<std::filesystem::path>::failure(current_exception_error("current_executable"));
        }
    }

    Result<std::filesystem::path> current_workspace()
    {
        try
        {
            std::error_code code;
            auto path = std::filesystem::current_path(code);
            if (code)
                return Result<std::filesystem::path>::failure(make_system_error(
                    "current_workspace", "std::filesystem::current_path", code));
            const bool directory = std::filesystem::is_directory(path, code);
            if (code)
                return Result<std::filesystem::path>::failure(make_system_error(
                    "current_workspace", "std::filesystem::is_directory", code, {{"path", path_text(path)}}));
            if (!directory)
                return Result<std::filesystem::path>::failure(make_error(
                    "current_workspace", "validation_error", "Workspace is not a directory", {{"path", path_text(path)}}));
            return Result<std::filesystem::path>::success(path.lexically_normal());
        }
        catch (...)
        {
            return Result<std::filesystem::path>::failure(current_exception_error("current_workspace"));
        }
    }

    Result<nlohmann::json> read_json_file(
        const std::filesystem::path& path,
        std::string_view operation)
    {
        try
        {
            errno = 0;
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                const int code = errno;
                auto details = nlohmann::json{{"path", path_text(path)}, {"rdstate", input.rdstate()}};
                return Result<nlohmann::json>::failure(code != 0
                    ? make_system_error(operation, "std::ifstream::open", {code, std::generic_category()}, std::move(details))
                    : make_error(operation, "io_error", "File stream failed to open", std::move(details)));
            }
            std::string text;
            std::array<char, 8192> buffer{};
            while (input)
            {
                errno = 0;
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                const int code = errno;
                text.append(buffer.data(), static_cast<std::size_t>(input.gcount()));
                if (input.bad() || (input.fail() && !input.eof()))
                {
                    auto details = nlohmann::json{{"path", path_text(path)}, {"rdstate", input.rdstate()}};
                    return Result<nlohmann::json>::failure(code != 0
                        ? make_system_error(operation, "std::ifstream::read", {code, std::generic_category()}, std::move(details))
                        : make_error(operation, "io_error", "File stream failed to read", std::move(details)));
                }
            }
            input.clear();
            errno = 0;
            input.close();
            if (input.fail())
            {
                const int code = errno;
                auto details = nlohmann::json{{"path", path_text(path)}, {"rdstate", input.rdstate()}};
                return Result<nlohmann::json>::failure(code != 0
                    ? make_system_error(operation, "std::ifstream::close", {code, std::generic_category()}, std::move(details))
                    : make_error(operation, "io_error", "File stream failed to close", std::move(details)));
            }
            return parse_json(text, operation, {{"path", path_text(path)}, {"api", "nlohmann::json::parse"}});
        }
        catch (const std::exception& exception)
        {
            return Result<nlohmann::json>::failure(exception_error(operation, exception, {{"path", path_text(path)}}));
        }
        catch (...)
        {
            return Result<nlohmann::json>::failure(current_exception_error(operation));
        }
    }
}
