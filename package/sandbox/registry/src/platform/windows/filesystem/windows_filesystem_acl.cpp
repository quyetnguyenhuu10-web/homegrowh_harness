#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_filesystem_acl.h"

#include "windows_filesystem_error.h"

#include <Aclapi.h>

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
            case permission::read_modify:
                return FILE_GENERIC_READ
                    | FILE_GENERIC_WRITE
                    | FILE_GENERIC_EXECUTE
                    | DELETE
                    | (directory ? FILE_DELETE_CHILD : 0);
            }
            throw std::invalid_argument("unknown sandbox permission");
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
            case permission::read_modify:
                return WRITE_DAC | WRITE_OWNER;
            }
            throw std::invalid_argument("unknown sandbox permission");
        }

        void append_win32_error(
            std::vector<registry_path_error>& path_errors,
            const std::filesystem::path& path,
            DWORD error)
        {
            path_errors.push_back({
                path,
                std::error_code(
                    static_cast<int>(error),
                    std::system_category()),
            });
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
                throw_win32("GetNamedSecurityInfoW", read_result);
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
                throw_win32("SetEntriesInAclW", acl_result);
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
                throw_win32("SetNamedSecurityInfoW", write_result);
        }

        bool reconcile_acl(
            const std::filesystem::path& path,
            PSID sid,
            permission access)
        {
            std::error_code error;
            const bool directory = std::filesystem::is_directory(path, error);
            if (error)
                throw std::system_error(error, "could not inspect sandbox registry path");

            const ACCESS_MASK mask = access_mask(access, directory);
            if (compatible_acl(path, sid, access, directory))
                return false;

            apply_acl(path, sid, mask, directory);
            if (!compatible_acl(path, sid, access, directory))
            {
                throw std::runtime_error(
                    "sandbox registry ACL verification failed after update");
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
            append_win32_error(path_errors, path, GetLastError());
            return false;
        }

        if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
        {
            append_win32_error(path_errors, path, ERROR_NOT_SUPPORTED);
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
            append_win32_error(path_errors, path, GetLastError());
            return false;
        }

        BY_HANDLE_FILE_INFORMATION information{};
        if (!GetFileInformationByHandle(handle, &information))
        {
            const DWORD error = GetLastError();
            CloseHandle(handle);
            append_win32_error(path_errors, path, error);
            return false;
        }

        if (!CloseHandle(handle))
        {
            append_win32_error(path_errors, path, GetLastError());
            return false;
        }

        if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0
            && information.nNumberOfLinks > 1)
        {
            append_win32_error(path_errors, path, ERROR_NOT_SUPPORTED);
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
            throw_win32("GetNamedSecurityInfoW", result);
        local_memory descriptor_memory(descriptor);

        if (dacl == nullptr)
            return false;

        bool compatible_allow = false;
        for (DWORD index = 0; index < dacl->AceCount; ++index)
        {
            void* raw_ace = nullptr;
            if (!GetAce(dacl, index, &raw_ace))
                throw_win32("GetAce", GetLastError());

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
        catch (const std::system_error& exception)
        {
            path_errors.push_back({root, exception.code()});
            return false;
        }

        std::error_code error;
        const bool directory = std::filesystem::is_directory(root, error);
        if (error)
            throw std::system_error(error, "could not inspect sandbox registry root");
        if (!directory)
            return updated;

        std::error_code iterator_error;
        std::filesystem::recursive_directory_iterator iterator(root, iterator_error);
        const std::filesystem::recursive_directory_iterator end;
        if (iterator_error)
        {
            path_errors.push_back({root, iterator_error});
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
                    path_errors.push_back({path, directory_error});
            }
            else
            {
                try
                {
                    updated = reconcile_acl(path, sid, access) || updated;
                }
                catch (const std::system_error& exception)
                {
                    path_errors.push_back({path, exception.code()});
                }
            }

            iterator.increment(iterator_error);
            if (iterator_error)
            {
                path_errors.push_back({path, iterator_error});
                iterator_error.clear();
            }
        }
        return updated;
    }
}
