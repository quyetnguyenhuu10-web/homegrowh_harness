#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "state.h"

#include "identity.h"
#include "error.h"

#include <Windows.h>

#include <array>
#include <cerrno>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace sandbox::detail::filesystem::windows
{
    namespace
    {
        constexpr std::wstring_view registry_mutex_name =
            L"Local\\HomegrowphHarness.Sandbox.Registry";
        constexpr std::wstring_view registry_state_override =
            L"HOMEGROWPH_SANDBOX_REGISTRY_STATE";

        std::string utf8(std::wstring_view input)
        {
            if (input.empty())
                return {};
            const int size = WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                input.data(),
                static_cast<int>(input.size()),
                nullptr,
                0,
                nullptr,
                nullptr);
            if (size <= 0)
                throw_win32("WideCharToMultiByte(size)", GetLastError());

            std::string output(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(
                    CP_UTF8,
                    WC_ERR_INVALID_CHARS,
                    input.data(),
                    static_cast<int>(input.size()),
                    output.data(),
                    size,
                    nullptr,
                    nullptr) <= 0)
            {
                throw_win32("WideCharToMultiByte", GetLastError());
            }
            return output;
        }

        std::wstring wide(std::string_view input)
        {
            if (input.empty())
                return {};
            const int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                input.data(),
                static_cast<int>(input.size()),
                nullptr,
                0);
            if (size <= 0)
                throw_win32("MultiByteToWideChar(size)", GetLastError());

            std::wstring output(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    input.data(),
                    static_cast<int>(input.size()),
                    output.data(),
                    size) <= 0)
            {
                throw_win32("MultiByteToWideChar", GetLastError());
            }
            return output;
        }

        std::optional<std::wstring> environment_value(std::wstring_view name)
        {
            const std::wstring variable(name);
            const DWORD required = GetEnvironmentVariableW(
                variable.c_str(),
                nullptr,
                0);
            if (required == 0)
            {
                const DWORD error = GetLastError();
                if (error == ERROR_ENVVAR_NOT_FOUND)
                    return std::nullopt;
                throw_win32("GetEnvironmentVariableW(size)", error);
            }

            std::wstring value(static_cast<std::size_t>(required), L'\0');
            const DWORD written = GetEnvironmentVariableW(
                variable.c_str(),
                value.data(),
                required);
            if (written == 0)
                throw_win32("GetEnvironmentVariableW", GetLastError());
            if (written >= required)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "GetEnvironmentVariableW",
                        "concurrent_change",
                        "environment variable changed while it was being read",
                        {
                            {"variable", utf8(variable)},
                            {"buffer_size", required},
                            {"required_size", written},
                        }));
            }
            value.resize(written);
            return value;
        }
    }

    registry_state_lock::registry_state_lock()
    {
        HANDLE handle = CreateMutexW(
            nullptr,
            FALSE,
            std::wstring(registry_mutex_name).c_str());
        if (handle == nullptr)
            throw_win32("CreateMutexW(sandbox registry)", GetLastError());

        const DWORD wait = WaitForSingleObject(handle, INFINITE);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        {
            const DWORD error = wait == WAIT_FAILED
                ? GetLastError()
                : ERROR_GEN_FAILURE;
            Error primary = sandbox::detail::make_native_error(
                "WaitForSingleObject(sandbox registry)",
                error);
            if (!CloseHandle(handle))
            {
                Error cleanup = sandbox::detail::make_native_error(
                    "CloseHandle(sandbox registry mutex)",
                    GetLastError());
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "acquire_registry_lock",
                        "operation_failed",
                        "registry lock acquisition failed and mutex cleanup also failed",
                        nullptr,
                        {std::move(primary), std::move(cleanup)}));
            }
            sandbox::detail::throw_error(std::move(primary));
        }

        handle_ = handle;
        locked_ = true;
    }

    registry_state_lock::~registry_state_lock()
    {
        if (handle_ == nullptr)
            return;
        HANDLE handle = static_cast<HANDLE>(handle_);
        if (locked_)
            ReleaseMutex(handle);
        CloseHandle(handle);
    }

    std::optional<Error> registry_state_lock::close()
    {
        if (handle_ == nullptr)
            return std::nullopt;

        HANDLE handle = static_cast<HANDLE>(handle_);
        std::vector<Error> errors;
        if (locked_ && !ReleaseMutex(handle))
        {
            errors.push_back(sandbox::detail::make_native_error(
                "ReleaseMutex(sandbox registry)",
                GetLastError()));
        }
        locked_ = false;

        if (!CloseHandle(handle))
        {
            errors.push_back(sandbox::detail::make_native_error(
                "CloseHandle(sandbox registry mutex)",
                GetLastError()));
        }
        handle_ = nullptr;

        if (errors.empty())
            return std::nullopt;
        if (errors.size() == 1)
            return std::move(errors.front());
        return sandbox::detail::make_error(
            "close_registry_lock",
            "operation_failed",
            "multiple registry lock cleanup operations failed",
            nullptr,
            std::move(errors));
    }

    std::filesystem::path registry_state_path()
    {
        if (const auto override_path = environment_value(registry_state_override))
            return std::filesystem::path(*override_path);

        const auto local_app_data = environment_value(L"LOCALAPPDATA");
        if (!local_app_data || local_app_data->empty())
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "registry_state_path",
                    "environment_error",
                    "LOCALAPPDATA is required for durable sandbox registry state",
                    {{"variable", "LOCALAPPDATA"}}));
        }
        return std::filesystem::path(*local_app_data)
            / std::wstring(capability_signature)
            / "sandbox-registry.state";
    }

    registry_state load_registry_state(const std::filesystem::path& path)
    {
        std::error_code exists_error;
        if (!std::filesystem::exists(path, exists_error))
        {
            if (exists_error)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_system_error(
                        "std::filesystem::exists",
                        exists_error,
                        path));
            }
            return {};
        }

        errno = 0;
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            const int native_error = errno;
            if (native_error != 0)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_system_error(
                        "open_registry_state",
                        std::error_code(native_error, std::generic_category()),
                        path));
            }
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "open_registry_state",
                    "io_error",
                    "could not open sandbox registry state",
                    {{"path", sandbox::detail::error_path_text(path)}}));
        }

        std::string content;
        std::array<char, 8192> buffer{};
        for (;;)
        {
            errno = 0;
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const int native_error = errno;
            const auto count = file.gcount();
            if (count > 0)
                content.append(buffer.data(), static_cast<std::size_t>(count));
            if (file.bad() || (file.fail() && !file.eof()))
            {
                Error failure = native_error != 0
                    ? sandbox::detail::make_system_error(
                        "read_registry_state",
                        std::error_code(native_error, std::generic_category()), path)
                    : sandbox::detail::make_error(
                        "read_registry_state", "io_error",
                        "Registry state stream could not be read");
                failure.data.push_back({
                    {"api", "std::ifstream::read"},
                    {"path", sandbox::detail::error_path_text(path)},
                    {"rdstate", static_cast<int>(file.rdstate())},
                    {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())},
                });
                sandbox::detail::throw_error(std::move(failure));
            }
            if (file.eof())
                break;
        }
        std::istringstream input(content);

        std::string signature;
        int schema = 0;
        if (!(input >> std::quoted(signature) >> schema)
            || signature != utf8(capability_signature)
            || schema != registry_schema_version)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "load_registry_state",
                    "invalid_state",
                    "sandbox registry state header is invalid",
                    {{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())}}));
        }

        registry_state state;
        while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
        {
            std::string canonical_path;
            std::string access;
            std::string capability_name;
            std::string sid;
            int acl_ready = 0;
            int stored_tree_version = 0;
            if (!(input
                    >> std::quoted(canonical_path)
                    >> std::quoted(access)
                    >> std::quoted(capability_name)
                    >> std::quoted(sid)
                    >> acl_ready
                    >> stored_tree_version))
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "load_registry_state",
                        "invalid_state",
                        "sandbox registry state entry is invalid",
                        {{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())}}));
            }
            if (access == "read_modify")
                access = "read_write";

            if (canonical_path.empty()
                || capability_name.empty()
                || sid.empty()
                || (access != "read_only" && access != "read_write")
                || (acl_ready != 0 && acl_ready != 1)
                || stored_tree_version < 0)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "load_registry_state",
                        "invalid_state",
                        "sandbox registry state entry is incomplete",
                        {{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())}}));
            }

            state.entries.push_back({
                wide(canonical_path),
                access,
                wide(capability_name),
                wide(sid),
                acl_ready == 1,
                stored_tree_version,
            });
        }

        for (std::size_t left = 0; left < state.entries.size(); ++left)
        {
            for (std::size_t right = left + 1;
                 right < state.entries.size();
                 ++right)
            {
                if (_wcsicmp(
                        state.entries[left].canonical_path.c_str(),
                        state.entries[right].canonical_path.c_str()) == 0
                    && state.entries[left].access == state.entries[right].access)
                {
                    sandbox::detail::throw_error(
                        sandbox::detail::make_error(
                            "load_registry_state",
                            "invalid_state",
                            "sandbox registry state contains duplicate entries",
                            {{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())}}));
                }
            }
        }
        return state;
    }

    void save_registry_state(
        const std::filesystem::path& path,
        const registry_state& state)
    {
        const std::filesystem::path parent = path.parent_path();
        if (!parent.empty())
        {
            std::error_code create_error;
            std::filesystem::create_directories(parent, create_error);
            if (create_error)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_system_error(
                        "std::filesystem::create_directories",
                        create_error,
                        parent));
            }
        }

        std::filesystem::path temporary = path;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        {
            errno = 0;
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                const int native_error = errno;
                if (native_error != 0)
                {
                    sandbox::detail::throw_error(
                        sandbox::detail::make_system_error(
                            "create_registry_state_temp",
                            std::error_code(native_error, std::generic_category()),
                            temporary));
                }
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "create_registry_state_temp",
                        "io_error",
                        "could not create sandbox registry state temp file",
                        {{"path", sandbox::detail::error_path_text(temporary)}}));
            }

            output
                << std::quoted(utf8(capability_signature))
                << ' '
                << registry_schema_version
                << '\n';
            for (const registry_entry& entry : state.entries)
            {
                output
                    << std::quoted(utf8(entry.canonical_path)) << ' '
                    << std::quoted(entry.access) << ' '
                    << std::quoted(utf8(entry.capability_name)) << ' '
                    << std::quoted(utf8(entry.sid)) << ' '
                    << (entry.acl_ready ? 1 : 0) << ' '
                    << entry.tree_version
                    << '\n';
            }
            output.flush();
            if (!output)
            {
                const int native_error = errno;
                if (native_error != 0)
                {
                    sandbox::detail::throw_error(
                        sandbox::detail::make_system_error(
                            "write_registry_state",
                            std::error_code(native_error, std::generic_category()),
                            temporary));
                }
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "write_registry_state",
                        "io_error",
                        "could not write sandbox registry state",
                        {{"path", sandbox::detail::error_path_text(temporary)}}));
            }
        }

        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            sandbox::Error failure = sandbox::detail::make_error(
                "save_registry_state",
                "commit_failed",
                "could not commit sandbox registry state",
                {
                    {"path", sandbox::detail::error_path_text(path)},
                    {"temporary_path", sandbox::detail::error_path_text(temporary)},
                },
                {sandbox::detail::make_native_error(
                    "MoveFileExW",
                    error,
                    path)});

            if (!DeleteFileW(temporary.c_str()))
            {
                const DWORD cleanup_error = GetLastError();
                if (cleanup_error != ERROR_FILE_NOT_FOUND)
                {
                    failure.causes.push_back(
                        sandbox::detail::make_native_error(
                            "DeleteFileW",
                            cleanup_error,
                            temporary));
                }
            }
            sandbox::detail::throw_error(std::move(failure));
        }
    }

    registry_entry* find_registry_entry(
        registry_state& state,
        const std::filesystem::path& canonical_path,
        permission access)
    {
        const std::string wanted_access(permission_name(access));
        for (registry_entry& entry : state.entries)
        {
            if (entry.access == wanted_access
                && _wcsicmp(
                    entry.canonical_path.c_str(),
                    canonical_path.c_str()) == 0)
            {
                return &entry;
            }
        }
        return nullptr;
    }
}
