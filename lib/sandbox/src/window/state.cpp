#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "state.h"

#include "identity.h"
#include "error.h"

#include <Windows.h>

#include <fstream>
#include <iomanip>
#include <optional>
#include <stdexcept>
#include <string>

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
            if (written == 0 || written >= required)
                throw_win32("GetEnvironmentVariableW", GetLastError());
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
            CloseHandle(handle);
            throw_win32("WaitForSingleObject(sandbox registry)", error);
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

    std::filesystem::path registry_state_path()
    {
        if (const auto override_path = environment_value(registry_state_override))
            return std::filesystem::path(*override_path);

        const auto local_app_data = environment_value(L"LOCALAPPDATA");
        if (!local_app_data || local_app_data->empty())
        {
            throw std::runtime_error(
                "LOCALAPPDATA is required for durable sandbox registry state");
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
                throw std::system_error(
                    exists_error,
                    "could not inspect sandbox registry state path");
            }
            return {};
        }

        std::ifstream input(path, std::ios::binary);
        if (!input)
            throw std::runtime_error("could not open sandbox registry state");

        std::string signature;
        int schema = 0;
        if (!(input >> std::quoted(signature) >> schema)
            || signature != utf8(capability_signature)
            || schema != registry_schema_version)
        {
            throw std::runtime_error("sandbox registry state header is invalid");
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
                throw std::runtime_error("sandbox registry state entry is invalid");
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
                throw std::runtime_error("sandbox registry state entry is incomplete");
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
                    throw std::runtime_error(
                        "sandbox registry state contains duplicate entries");
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
                throw std::system_error(
                    create_error,
                    "could not create sandbox registry state directory");
            }
        }

        std::filesystem::path temporary = path;
        temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
        {
            std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("could not create sandbox registry state temp file");

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
                throw std::runtime_error("could not write sandbox registry state");
        }

        if (!MoveFileExW(
                temporary.c_str(),
                path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD error = GetLastError();
            DeleteFileW(temporary.c_str());
            throw_win32("MoveFileExW(sandbox registry state)", error);
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
