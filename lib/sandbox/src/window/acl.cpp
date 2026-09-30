#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "acl.h"

#include "error.h"

#include <Aclapi.h>
#include <sddl.h>

#include <memory>
#include <stdexcept>
#include <system_error>

namespace sandbox::detail::filesystem::windows
{
    namespace
    {
        struct local_free_deleter
        {
            void operator()(void* pointer) const noexcept
            {
                if (pointer != nullptr)
                    LocalFree(pointer);
            }
        };

        using local_memory = std::unique_ptr<void, local_free_deleter>;

        ACCESS_MASK access_mask(permission access, bool directory)
        {
            switch (access)
            {
            case permission::read_only:
                return FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
            case permission::read_write:
                return FILE_GENERIC_READ
                    | FILE_GENERIC_WRITE
                    | FILE_GENERIC_EXECUTE
                    | DELETE
                    | (directory ? FILE_DELETE_CHILD : 0);
            }
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "access_mask",
                    "invalid_argument",
                    "unknown sandbox permission"));
        }

        ACCESS_MASK forbidden_access_mask(permission access)
        {
            switch (access)
            {
            case permission::read_only:
                return FILE_WRITE_DATA
                    | FILE_APPEND_DATA
                    | FILE_WRITE_EA
                    | FILE_WRITE_ATTRIBUTES
                    | DELETE
                    | FILE_DELETE_CHILD
                    | WRITE_DAC
                    | WRITE_OWNER;
            case permission::read_write:
                return WRITE_DAC | WRITE_OWNER;
            }
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "forbidden_access_mask",
                    "invalid_argument",
                    "unknown sandbox permission"));
        }

        void append_win32_error(
            std::vector<registry_path_error>& path_errors,
            const std::filesystem::path& path,
            std::string_view operation,
            DWORD error)
        {
            path_errors.push_back({
                path,
                sandbox::detail::make_native_error(
                    std::string(operation),
                    error,
                    path),
            });
        }

        void append_path_error(
            std::vector<registry_path_error>& path_errors,
            const std::filesystem::path& path,
            std::string operation,
            std::string type,
            std::string message,
            nlohmann::json data = nlohmann::json::object())
        {
            data["path"] = sandbox::detail::error_path_text(path);
            path_errors.push_back({
                path,
                sandbox::detail::make_error(
                    std::move(operation),
                    std::move(type),
                    std::move(message),
                    std::move(data)),
            });
        }

        bool contains_capability_sid(
            const std::filesystem::path& path,
            PACL dacl,
            PSID sid)
        {
            if (dacl == nullptr)
                return false;

            for (DWORD index = 0; index < dacl->AceCount; ++index)
            {
                void* raw_ace = nullptr;
                if (!GetAce(dacl, index, &raw_ace))
                    throw_win32("GetAce", GetLastError(), path);

                auto* header = static_cast<ACE_HEADER*>(raw_ace);
                if (header->AceType != ACCESS_ALLOWED_ACE_TYPE
                    && header->AceType != ACCESS_DENIED_ACE_TYPE)
                {
                    continue;
                }

                auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
                PSID ace_sid = reinterpret_cast<PSID>(&ace->SidStart);
                if (EqualSid(ace_sid, sid))
                    return true;
            }
            return false;
        }

        bool has_capability_sid(
            const std::filesystem::path& path,
            PSID sid)
        {
            PACL dacl = nullptr;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const DWORD result = GetNamedSecurityInfoW(
                const_cast<LPWSTR>(path.c_str()),
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                &dacl,
                nullptr,
                &descriptor);
            if (result != ERROR_SUCCESS)
                throw_win32("GetNamedSecurityInfoW", result, path);
            local_memory descriptor_memory(descriptor);
            return contains_capability_sid(path, dacl, sid);
        }

        bool revoke_acl(
            const std::filesystem::path& path,
            PSID sid)
        {
            PACL old_dacl = nullptr;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const DWORD read_result = GetNamedSecurityInfoW(
                const_cast<LPWSTR>(path.c_str()),
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                &old_dacl,
                nullptr,
                &descriptor);
            if (read_result != ERROR_SUCCESS)
                throw_win32("GetNamedSecurityInfoW", read_result, path);
            local_memory descriptor_memory(descriptor);

            if (!contains_capability_sid(path, old_dacl, sid))
                return false;

            EXPLICIT_ACCESSW entry{};
            entry.grfAccessMode = REVOKE_ACCESS;
            entry.Trustee.pMultipleTrustee = nullptr;
            entry.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
            entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            entry.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
            entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);

            PACL updated_dacl = nullptr;
            const DWORD acl_result = SetEntriesInAclW(
                1,
                &entry,
                old_dacl,
                &updated_dacl);
            if (acl_result != ERROR_SUCCESS)
                throw_win32("SetEntriesInAclW(REVOKE_ACCESS)", acl_result, path);
            local_memory acl_memory(updated_dacl);

            const DWORD write_result = SetNamedSecurityInfoW(
                const_cast<LPWSTR>(path.c_str()),
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                updated_dacl,
                nullptr);
            if (write_result != ERROR_SUCCESS)
                throw_win32("SetNamedSecurityInfoW(REVOKE_ACCESS)", write_result, path);

            if (has_capability_sid(path, sid))
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "verify_acl_revoke",
                        "acl_verification_error",
                        "sandbox capability ACE remained after revoke",
                        {{"path", sandbox::detail::error_path_text(path)}}));
            }
            return true;
        }

        void apply_acl(
            const std::filesystem::path& path,
            PSID sid,
            ACCESS_MASK mask,
            bool directory)
        {
            PACL old_dacl = nullptr;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            const DWORD read_result = GetNamedSecurityInfoW(
                const_cast<LPWSTR>(path.c_str()),
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                &old_dacl,
                nullptr,
                &descriptor);
            if (read_result != ERROR_SUCCESS)
                throw_win32("GetNamedSecurityInfoW", read_result, path);
            local_memory descriptor_memory(descriptor);

            EXPLICIT_ACCESSW entry{};
            entry.grfAccessPermissions = mask;
            entry.grfAccessMode = SET_ACCESS;
            entry.grfInheritance = directory
                ? SUB_CONTAINERS_AND_OBJECTS_INHERIT
                : NO_INHERITANCE;
            entry.Trustee.pMultipleTrustee = nullptr;
            entry.Trustee.MultipleTrusteeOperation = NO_MULTIPLE_TRUSTEE;
            entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            entry.Trustee.TrusteeType = TRUSTEE_IS_UNKNOWN;
            entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);

            PACL updated_dacl = nullptr;
            const DWORD acl_result = SetEntriesInAclW(
                1,
                &entry,
                old_dacl,
                &updated_dacl);
            if (acl_result != ERROR_SUCCESS)
                throw_win32("SetEntriesInAclW", acl_result, path);
            local_memory acl_memory(updated_dacl);

            const DWORD write_result = SetNamedSecurityInfoW(
                const_cast<LPWSTR>(path.c_str()),
                SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                updated_dacl,
                nullptr);
            if (write_result != ERROR_SUCCESS)
                throw_win32("SetNamedSecurityInfoW", write_result, path);
        }

        bool reconcile_acl(
            const std::filesystem::path& path,
            PSID sid,
            permission access)
        {
            std::error_code error;
            const bool directory = std::filesystem::is_directory(path, error);
            if (error)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_system_error(
                        "std::filesystem::is_directory",
                        error,
                        path));
            }

            const ACCESS_MASK mask = access_mask(access, directory);
            if (compatible_acl(path, sid, access, directory))
                return false;

            apply_acl(path, sid, mask, directory);
            if (!compatible_acl(path, sid, access, directory))
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "verify_acl_update",
                        "acl_verification_error",
                        "sandbox registry ACL verification failed after update",
                        {{"path", sandbox::detail::error_path_text(path)}}));
            }
            return true;
        }
    }

    bool inspect_registry_path(
        const std::filesystem::path& path,
        std::vector<registry_path_error>& path_errors)
    {
        const DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES)
        {
            append_win32_error(
                path_errors,
                path,
                "GetFileAttributesW",
                GetLastError());
            return false;
        }

        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            append_path_error(
                path_errors,
                path,
                "inspect_registry_path",
                "unsupported_path",
                "reparse points are not supported",
                {
                    {"reason", "reparse_point"},
                    {"code", static_cast<int>(ERROR_NOT_SUPPORTED)},
                    {"category", std::system_category().name()},
                });
            return false;
        }

        const DWORD flags = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0
            ? FILE_FLAG_BACKUP_SEMANTICS
            : FILE_ATTRIBUTE_NORMAL;
        HANDLE handle = CreateFileW(
            path.c_str(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr,
            OPEN_EXISTING,
            flags,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE)
        {
            append_win32_error(
                path_errors,
                path,
                "CreateFileW",
                GetLastError());
            return false;
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(handle, &information))
        {
            const DWORD primary_code = GetLastError();
            sandbox::Error primary = sandbox::detail::make_native_error(
                "GetFileInformationByHandle",
                primary_code,
                path);
            if (!CloseHandle(handle))
            {
                sandbox::Error cleanup = sandbox::detail::make_native_error(
                    "CloseHandle",
                    GetLastError(),
                    path);
                path_errors.push_back({
                    path,
                    sandbox::detail::make_error(
                        "inspect_registry_path",
                        "operation_failed",
                        "filesystem inspection and handle cleanup failed",
                        {{"path", sandbox::detail::error_path_text(path)}},
                        {std::move(primary), std::move(cleanup)}),
                });
                return false;
            }
            path_errors.push_back({path, std::move(primary)});
            return false;
        }

        if (!CloseHandle(handle))
        {
            append_win32_error(
                path_errors,
                path,
                "CloseHandle",
                GetLastError());
            return false;
        }

        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0
            && information.nNumberOfLinks > 1)
        {
            append_path_error(
                path_errors,
                path,
                "inspect_registry_path",
                "unsupported_path",
                "hard-linked files are not supported",
                {
                    {"reason", "hard_link"},
                    {"code", static_cast<int>(ERROR_NOT_SUPPORTED)},
                    {"category", std::system_category().name()},
                });
            return false;
        }

        return true;
    }

    bool compatible_acl(
        const std::filesystem::path& path,
        PSID sid,
        permission access,
        bool directory)
    {
        const ACCESS_MASK required_mask = access_mask(access, directory);
        const ACCESS_MASK forbidden_mask = forbidden_access_mask(access);
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const DWORD result = GetNamedSecurityInfoW(
            const_cast<LPWSTR>(path.c_str()),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &dacl,
            nullptr,
            &descriptor);
        if (result != ERROR_SUCCESS)
            throw_win32("GetNamedSecurityInfoW", result, path);
        local_memory descriptor_memory(descriptor);

        if (dacl == nullptr)
            return false;

        bool compatible_allow = false;
        for (DWORD index = 0; index < dacl->AceCount; ++index)
        {
            void* raw_ace = nullptr;
            if (!GetAce(dacl, index, &raw_ace))
                throw_win32("GetAce", GetLastError(), path);

            auto* header = static_cast<ACE_HEADER*>(raw_ace);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE
                && header->AceType != ACCESS_DENIED_ACE_TYPE)
            {
                continue;
            }

            auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
            PSID ace_sid = reinterpret_cast<PSID>(&ace->SidStart);
            if (!EqualSid(ace_sid, sid))
                continue;

            if (header->AceType == ACCESS_DENIED_ACE_TYPE
                && (ace->Mask & required_mask) != 0)
            {
                return false;
            }

            if (header->AceType == ACCESS_ALLOWED_ACE_TYPE
                && (ace->Mask & forbidden_mask) != 0)
            {
                return false;
            }

            if (header->AceType == ACCESS_ALLOWED_ACE_TYPE
                && (ace->Mask & required_mask) == required_mask)
            {
                if (!directory
                    || ((header->AceFlags & OBJECT_INHERIT_ACE) != 0
                        && (header->AceFlags & CONTAINER_INHERIT_ACE) != 0))
                {
                    compatible_allow = true;
                }
            }
        }
        return compatible_allow;
    }

    bool reconcile_tree(
        const std::filesystem::path& root,
        PSID sid,
        permission access,
        std::vector<registry_path_error>& path_errors)
    {
        /*
         * This is an explicit registration-time baseline operation, not a
         * self-healing watcher. Do not connect ReadDirectoryChangesW,
         * FILE_NOTIFY_CHANGE_SECURITY, USN Journal, or any other automatic
         * change detector here. Post-registration ACL changes may be deliberate.
         */
        bool updated = false;
        try
        {
            updated = reconcile_acl(root, sid, access);
        }
        catch (...)
        {
            path_errors.push_back({root, sandbox::detail::capture_exception("filesystem", std::current_exception())});
            return false;
        }

        std::error_code error;
        const bool directory = std::filesystem::is_directory(root, error);
        if (error)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_system_error(
                    "std::filesystem::is_directory",
                    error,
                    root));
        }
        if (!directory)
            return updated;

        std::error_code iterator_error;
        std::filesystem::recursive_directory_iterator iterator(root, iterator_error);
        const std::filesystem::recursive_directory_iterator end;
        if (iterator_error)
        {
            path_errors.push_back({
                root,
                sandbox::detail::make_system_error(
                    "recursive_directory_iterator",
                    iterator_error,
                    root),
            });
            return updated;
        }

        while (iterator != end)
        {
            const std::filesystem::path path = iterator->path();
            if (!inspect_registry_path(path, path_errors))
            {
                std::error_code directory_error;
                if (iterator->is_directory(directory_error))
                    iterator.disable_recursion_pending();
                if (directory_error)
                {
                    path_errors.push_back({
                        path,
                        sandbox::detail::make_system_error(
                            "directory_entry::is_directory",
                            directory_error,
                            path),
                    });
                }
            }
            else
            {
                try
                {
                    updated = reconcile_acl(path, sid, access) || updated;
                }
                catch (...)
                {
                    path_errors.push_back({path, sandbox::detail::capture_exception("filesystem", std::current_exception())});
                }
            }

            iterator.increment(iterator_error);
            if (iterator_error)
            {
                path_errors.push_back({
                    path,
                    sandbox::detail::make_system_error(
                        "recursive_directory_iterator::increment",
                        iterator_error,
                        path),
                });
                iterator_error.clear();
            }
        }
        return updated;
    }

    std::optional<registry_path_error> release_tree(
        const std::filesystem::path& root,
        const std::wstring& sid_text)
    {
        PSID sid = nullptr;
        if (!ConvertStringSidToSidW(sid_text.c_str(), &sid))
        {
            return registry_path_error{
                root,
                sandbox::detail::make_native_error(
                    "ConvertStringSidToSidW",
                    GetLastError(),
                    root),
            };
        }
        local_memory sid_memory(sid);

        const DWORD root_attributes = GetFileAttributesW(root.c_str());
        if (root_attributes == INVALID_FILE_ATTRIBUTES)
        {
            return registry_path_error{
                root,
                sandbox::detail::make_native_error(
                    "GetFileAttributesW",
                    GetLastError(),
                    root),
            };
        }
        if ((root_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            return registry_path_error{
                root,
                sandbox::detail::make_error(
                    "release_tree",
                    "unsupported_path",
                    "reparse points are not supported",
                    {
                        {"path", sandbox::detail::error_path_text(root)},
                        {"reason", "reparse_point"},
                        {"code", static_cast<int>(ERROR_NOT_SUPPORTED)},
                        {"category", std::system_category().name()},
                    }),
            };
        }

        try
        {
            revoke_acl(root, sid);
        }
        catch (...)
        {
            return registry_path_error{root, sandbox::detail::capture_exception("filesystem", std::current_exception())};
        }

        if ((root_attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            return std::nullopt;

        std::error_code iterator_error;
        std::filesystem::recursive_directory_iterator iterator(root, iterator_error);
        const std::filesystem::recursive_directory_iterator end;
        if (iterator_error)
        {
            return registry_path_error{
                root,
                sandbox::detail::make_system_error(
                    "recursive_directory_iterator",
                    iterator_error,
                    root),
            };
        }

        while (iterator != end)
        {
            const std::filesystem::path path = iterator->path();
            const DWORD attributes = GetFileAttributesW(path.c_str());
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                return registry_path_error{
                    path,
                    sandbox::detail::make_native_error(
                        "GetFileAttributesW",
                        GetLastError(),
                        path),
                };
            }
            else if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
            {
                if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                    iterator.disable_recursion_pending();
            }
            else
            {
                try
                {
                    revoke_acl(path, sid);
                }
                catch (...)
                {
                    return registry_path_error{path, sandbox::detail::capture_exception("filesystem", std::current_exception())};
                }
            }

            iterator.increment(iterator_error);
            if (iterator_error)
            {
                return registry_path_error{
                    path,
                    sandbox::detail::make_system_error(
                        "recursive_directory_iterator::increment",
                        iterator_error,
                        path),
                };
            }
        }
        return std::nullopt;
    }
}
