#include "../src/api/registry.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>

#if defined(__linux__)
#include <cerrno>
#include <cstdlib>
#endif

#ifdef _WIN32
#include <Windows.h>
#include <Aclapi.h>
#include <sddl.h>
#endif

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    bool has_path_error(
        const sandbox::registry_result& result,
        const std::filesystem::path& path,
        int error_value)
    {
        for (const auto& item : result.path_errors)
        {
            if (item.path == path && item.error.value() == error_value)
                return true;
        }
        return false;
    }

    bool has_path_error(
        const sandbox::release_result& result,
        const std::filesystem::path& path,
        int error_value)
    {
        for (const auto& item : result.path_errors)
        {
            if (item.path == path && item.error.value() == error_value)
                return true;
        }
        return false;
    }

#ifdef _WIN32
    struct local_free_deleter
    {
        void operator()(void* pointer) const noexcept
        {
            if (pointer != nullptr)
                LocalFree(pointer);
        }
    };

    using local_memory = std::unique_ptr<void, local_free_deleter>;

    ACCESS_MASK required_mask(sandbox::permission access, bool directory)
    {
        switch (access)
        {
        case sandbox::permission::read_only:
            return FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        case sandbox::permission::read_write:
            return FILE_GENERIC_READ
                | FILE_GENERIC_WRITE
                | FILE_GENERIC_EXECUTE
                | DELETE
                | (directory ? FILE_DELETE_CHILD : 0);
        }
        throw std::runtime_error("unknown test permission");
    }

    bool has_capability_acl(
        const std::filesystem::path& path,
        const std::wstring& sid_text,
        sandbox::permission access,
        bool require_inherited)
    {
        PSID sid = nullptr;
        if (!ConvertStringSidToSidW(sid_text.c_str(), &sid))
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "ConvertStringSidToSidW");
        local_memory sid_memory(sid);

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
            throw std::system_error(
                static_cast<int>(result),
                std::system_category(),
                "GetNamedSecurityInfoW");
        local_memory descriptor_memory(descriptor);

        std::error_code error;
        const bool directory = std::filesystem::is_directory(path, error);
        if (error)
            throw std::system_error(error, "could not inspect test path");
        const ACCESS_MASK mask = required_mask(access, directory);

        for (DWORD index = 0; dacl != nullptr && index < dacl->AceCount; ++index)
        {
            void* raw_ace = nullptr;
            if (!GetAce(dacl, index, &raw_ace))
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "GetAce");

            auto* header = static_cast<ACE_HEADER*>(raw_ace);
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
                continue;

            auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw_ace);
            PSID ace_sid = reinterpret_cast<PSID>(&ace->SidStart);
            if (!EqualSid(ace_sid, sid)
                || (ace->Mask & mask) != mask
                || (require_inherited && (header->AceFlags & INHERITED_ACE) == 0))
            {
                continue;
            }

            if (directory
                && ((header->AceFlags & OBJECT_INHERIT_ACE) == 0
                    || (header->AceFlags & CONTAINER_INHERIT_ACE) == 0))
            {
                continue;
            }
            return true;
        }
        return false;
    }

    void protect_dacl(const std::filesystem::path& path)
    {
        PACL dacl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const DWORD read_result = GetNamedSecurityInfoW(
            const_cast<LPWSTR>(path.c_str()),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &dacl,
            nullptr,
            &descriptor);
        if (read_result != ERROR_SUCCESS)
            throw std::system_error(
                static_cast<int>(read_result),
                std::system_category(),
                "GetNamedSecurityInfoW(protect_dacl)");
        local_memory descriptor_memory(descriptor);

        const DWORD write_result = SetNamedSecurityInfoW(
            const_cast<LPWSTR>(path.c_str()),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            dacl,
            nullptr);
        if (write_result != ERROR_SUCCESS)
            throw std::system_error(
                static_cast<int>(write_result),
                std::system_category(),
                "SetNamedSecurityInfoW(protect_dacl)");
    }

    void revoke_capability_acl(
        const std::filesystem::path& path,
        const std::wstring& sid_text)
    {
        PSID sid = nullptr;
        if (!ConvertStringSidToSidW(sid_text.c_str(), &sid))
        {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                "ConvertStringSidToSidW(revoke_capability_acl)");
        }
        local_memory sid_memory(sid);

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
        {
            throw std::system_error(
                static_cast<int>(read_result),
                std::system_category(),
                "GetNamedSecurityInfoW(revoke_capability_acl)");
        }
        local_memory descriptor_memory(descriptor);

        EXPLICIT_ACCESSW entry{};
        entry.grfAccessMode = REVOKE_ACCESS;
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
        {
            throw std::system_error(
                static_cast<int>(acl_result),
                std::system_category(),
                "SetEntriesInAclW(revoke_capability_acl)");
        }
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
        {
            throw std::system_error(
                static_cast<int>(write_result),
                std::system_category(),
                "SetNamedSecurityInfoW(revoke_capability_acl)");
        }
    }
#endif
}

int main()
{
#ifdef _WIN32
    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / "homegrowph-harness-sandbox-registry-test";
    const std::filesystem::path state_path =
        std::filesystem::temp_directory_path()
        / "homegrowph-harness-sandbox-registry-test.state";
    const std::filesystem::path failed_release_root =
        std::filesystem::temp_directory_path()
        / "homegrowph-harness-sandbox-release-failure-test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::filesystem::remove_all(failed_release_root, cleanup_error);
    std::filesystem::remove(state_path, cleanup_error);
    if (_wputenv_s(
            L"HOMEGROWPH_SANDBOX_REGISTRY_STATE",
            state_path.c_str()) != 0)
    {
        throw std::runtime_error("could not set sandbox registry test state path");
    }
    const auto existing_directory = root / "existing" / "nested";
    const auto existing_file = existing_directory / "before.txt";
    const auto protected_directory = root / "protected";
    const auto protected_existing_file = protected_directory / "before.txt";
    const auto hard_link_source = root / "hard-link-source.txt";
    const auto hard_link_alias = root / "hard-link-alias.txt";
    std::filesystem::create_directories(existing_directory);
    std::filesystem::create_directories(protected_directory);
    {
        std::ofstream output(existing_file);
        output << "created before registry";
    }
    {
        std::ofstream output(protected_existing_file);
        output << "created before registry under protected DACL";
    }
    {
        std::ofstream output(hard_link_source);
        output << "hard link source";
    }
    if (!CreateHardLinkW(
            hard_link_alias.c_str(),
            hard_link_source.c_str(),
            nullptr))
    {
        throw std::system_error(
            static_cast<int>(GetLastError()),
            std::system_category(),
            "CreateHardLinkW");
    }
    protect_dacl(protected_directory);

    try
    {
        const auto first = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, true);
        require(first.permissions.size() == 1, "first registration result size mismatch");
        require(!first.final_error, "first registration returned a final OS error");
        require(!first.permissions[0].reused, "first registration must establish durable baseline state");
        require(!first.permissions[0].sid.empty(), "first registration SID is empty");
        require(
            std::filesystem::exists(state_path),
            "durable sandbox registry state file was not created");
        require(
            first.permissions[0].capability_name.starts_with(L"HomegrowphHarness"),
            "capability signature mismatch");
        require(
            has_path_error(first, hard_link_source, ERROR_NOT_SUPPORTED),
            "hard-link source was not reported as a path-level OS error");
        require(
            has_path_error(first, hard_link_alias, ERROR_NOT_SUPPORTED),
            "hard-link alias was not reported as a path-level OS error");
        require(
            has_capability_acl(
                existing_directory,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "pre-existing directory did not receive read-only baseline ACL");
        require(
            has_capability_acl(
                existing_file,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "pre-existing file did not receive read-only baseline ACL");
        require(
            has_capability_acl(
                protected_directory,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "protected directory did not receive read-only baseline ACL");
        require(
            has_capability_acl(
                protected_existing_file,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "file under protected directory did not receive read-only baseline ACL");

        const auto protected_future_file = protected_directory / "after.txt";
        {
            std::ofstream output(protected_future_file);
            output << "created after registry under protected DACL";
        }
        require(
            has_capability_acl(
                protected_future_file,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                true),
            "new file under protected directory did not inherit repaired ACL");

        const auto future_directory = root / "future" / "nested";
        const auto future_file = future_directory / "after.txt";
        std::filesystem::create_directories(future_directory);
        {
            std::ofstream output(future_file);
            output << "created after registry";
        }
        require(
            has_capability_acl(
                future_directory,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                true),
            "new directory did not inherit read-only ACL");
        require(
            has_capability_acl(
                future_file,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                true),
            "new file did not inherit read-only ACL");

        const auto second = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, false);
        require(second.permissions.size() == 1, "second registration result size mismatch");
        require(second.permissions[0].reused, "existing compatible ACL was not reused");
        require(first.permissions[0].sid == second.permissions[0].sid, "read-only SID was not stable");
        require(
            first.permissions[0].capability_name == second.permissions[0].capability_name,
            "read-only capability name was not stable");

        revoke_capability_acl(root, first.permissions[0].sid);
        require(
            !has_capability_acl(
                root,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "test failed to remove read-only ACL before reuse check");

        const auto reuse_after_acl_change = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, false);
        require(
            reuse_after_acl_change.permissions[0].reused,
            "refresh=false did not remain a pure reuse operation");
        require(
            !has_capability_acl(
                root,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "refresh=false unexpectedly repaired a changed ACL");

        const auto refreshed = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, true);
        require(!refreshed.permissions[0].reused, "refresh=true was reported as reuse");
        require(
            has_capability_acl(
                root,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "refresh=true did not restore the changed ACL");

        const auto modify = sandbox::registry({
            {root, sandbox::permission::read_write},
        }, true);
        require(modify.permissions.size() == 1, "modify registration result size mismatch");
        require(
            modify.permissions[0].sid != first.permissions[0].sid,
            "read-only and read-write capabilities must be distinct");
        require(
            has_capability_acl(
                existing_directory,
                modify.permissions[0].sid,
                sandbox::permission::read_write,
                false),
            "pre-existing directory did not receive read-write baseline ACL");
        require(
            has_capability_acl(
                existing_file,
                modify.permissions[0].sid,
                sandbox::permission::read_write,
                false),
            "pre-existing file did not receive read-write baseline ACL");

        const auto modify_future_directory = root / "modify-future";
        const auto modify_future_file = modify_future_directory / "after.txt";
        std::filesystem::create_directories(modify_future_directory);
        {
            std::ofstream output(modify_future_file);
            output << "created after modify registry";
        }
        require(
            has_capability_acl(
                modify_future_directory,
                modify.permissions[0].sid,
                sandbox::permission::read_write,
                true),
            "new directory did not inherit read-write ACL");
        require(
            has_capability_acl(
                modify_future_file,
                modify.permissions[0].sid,
                sandbox::permission::read_write,
                true),
            "new file did not inherit read-write ACL");

        const auto modify_again = sandbox::registry({
            {root, sandbox::permission::read_write},
        }, false);
        require(modify_again.permissions[0].reused, "modify ACL was not reused");
        require(
            modify_again.permissions[0].sid == modify.permissions[0].sid,
            "read-write SID was not stable");

        const auto other_root = root / "other-registration";
        std::filesystem::create_directories(other_root);
        const auto other = sandbox::registry({
            {other_root, sandbox::permission::read_only},
        }, true);
        require(other.permissions.size() == 1, "other registration result size mismatch");
        require(
            has_capability_acl(
                other_root,
                other.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "other registration ACL was not established");

        const auto released = sandbox::release(root);
        require(!released.final_error, "release(path) returned a final OS error");
        require(released.path_errors.empty(), "release(path) returned a path OS error");
        require(
            !has_capability_acl(
                existing_file,
                first.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "release(path) left the read-only capability ACE behind");
        require(
            !has_capability_acl(
                existing_file,
                modify.permissions[0].sid,
                sandbox::permission::read_write,
                false),
            "release(path) left the read-write capability ACE behind");
        require(
            has_capability_acl(
                other_root,
                other.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "release(path) removed another registered capability");

        const auto released_read_only_reuse = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, false);
        require(
            released_read_only_reuse.permissions.empty(),
            "released read-only capability was still reusable");
        require(
            has_path_error(
                released_read_only_reuse,
                root,
                ERROR_FILE_NOT_FOUND),
            "released read-only capability did not preserve missing-state error");

        const auto released_read_write_reuse = sandbox::registry({
            {root, sandbox::permission::read_write},
        }, false);
        require(
            released_read_write_reuse.permissions.empty(),
            "released read-write capability was still reusable");
        require(
            has_path_error(
                released_read_write_reuse,
                root,
                ERROR_FILE_NOT_FOUND),
            "released read-write capability did not preserve missing-state error");

        const auto other_reuse = sandbox::registry({
            {other_root, sandbox::permission::read_only},
        }, false);
        require(
            other_reuse.permissions.size() == 1
                && other_reuse.permissions[0].reused,
            "release(path) broke another path's reuse state");

        const auto repeated_release = sandbox::release(root);
        require(!repeated_release.final_error, "repeated release(path) failed");
        require(repeated_release.path_errors.empty(), "repeated release(path) returned a path error");

        std::filesystem::create_directories(failed_release_root);
        const auto failed_release_registration = sandbox::registry({
            {failed_release_root, sandbox::permission::read_only},
        }, true);
        require(
            failed_release_registration.permissions.size() == 1,
            "failed-release setup registration failed");
        std::filesystem::remove_all(failed_release_root);

        const auto released_all = sandbox::release_all();
        require(!released_all.final_error, "release_all() returned a final OS error");
        require(
            has_path_error(
                released_all,
                failed_release_root,
                ERROR_FILE_NOT_FOUND)
                || has_path_error(
                    released_all,
                    failed_release_root,
                    ERROR_PATH_NOT_FOUND),
            "release_all() did not return the cleanup path error");
        require(
            !has_capability_acl(
                other_root,
                other.permissions[0].sid,
                sandbox::permission::read_only,
                false),
            "release_all() left a sandbox capability ACE behind");

        const auto released_all_reuse = sandbox::registry({
            {other_root, sandbox::permission::read_only},
        }, false);
        require(
            released_all_reuse.permissions.empty(),
            "release_all() left durable reuse state behind");
        require(
            has_path_error(
                released_all_reuse,
                other_root,
                ERROR_FILE_NOT_FOUND),
            "release_all() did not preserve missing-state error");

        std::filesystem::create_directories(failed_release_root);
        const auto failed_release_reuse = sandbox::registry({
            {failed_release_root, sandbox::permission::read_only},
        }, false);
        require(
            failed_release_reuse.permissions.size() == 1
                && failed_release_reuse.permissions[0].reused,
            "failed cleanup entry incorrectly advanced to state removal");

        const auto cleanup_failed_entry = sandbox::release_all();
        require(
            !cleanup_failed_entry.final_error,
            "second explicit release_all() returned a final OS error");
        require(
            cleanup_failed_entry.path_errors.empty(),
            "second explicit release_all() failed after the path was restored");

        const auto cleaned_failed_release_reuse = sandbox::registry({
            {failed_release_root, sandbox::permission::read_only},
        }, false);
        require(
            cleaned_failed_release_reuse.permissions.empty(),
            "successful cleanup did not remove durable state");
        require(
            has_path_error(
                cleaned_failed_release_reuse,
                failed_release_root,
                ERROR_FILE_NOT_FOUND),
            "successful cleanup left durable state reusable");

        const auto repeated_release_all = sandbox::release_all();
        require(!repeated_release_all.final_error, "repeated release_all() failed");
        require(
            repeated_release_all.path_errors.empty(),
            "repeated release_all() returned a path error");

        {
            std::ofstream output(root / "after-release.txt");
            output << "unrelated owner/user ACLs remain usable";
            require(static_cast<bool>(output), "release removed unrelated filesystem access");
        }

        std::filesystem::remove_all(root);
        std::filesystem::remove_all(failed_release_root, cleanup_error);
        std::filesystem::remove(state_path, cleanup_error);
        _wputenv_s(L"HOMEGROWPH_SANDBOX_REGISTRY_STATE", L"");
        std::cout << "sandbox registry tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::filesystem::remove_all(root, cleanup_error);
        std::filesystem::remove_all(failed_release_root, cleanup_error);
        std::filesystem::remove(state_path, cleanup_error);
        _wputenv_s(L"HOMEGROWPH_SANDBOX_REGISTRY_STATE", L"");
        std::cerr << "sandbox registry test failed: " << exception.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::filesystem::remove_all(root, cleanup_error);
        std::filesystem::remove_all(failed_release_root, cleanup_error);
        std::filesystem::remove(state_path, cleanup_error);
        _wputenv_s(L"HOMEGROWPH_SANDBOX_REGISTRY_STATE", L"");
        std::cerr << "sandbox registry test failed: unknown exception\n";
        return 1;
    }
#elif defined(__linux__)
    const std::filesystem::path root =
        std::filesystem::temp_directory_path()
        / "homegrowph-harness-sandbox-registry-linux-test";
    const std::filesystem::path state_path =
        std::filesystem::temp_directory_path()
        / "homegrowph-harness-sandbox-registry-linux-test.state";
    std::filesystem::path lock_path = state_path;
    lock_path += ".lock";

    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    std::filesystem::remove(state_path, cleanup_error);
    std::filesystem::remove(lock_path, cleanup_error);

    if (::setenv(
            "HOMEGROWPH_SANDBOX_REGISTRY_STATE",
            state_path.c_str(),
            1) != 0)
    {
        throw std::system_error(
            errno,
            std::generic_category(),
            "setenv(HOMEGROWPH_SANDBOX_REGISTRY_STATE)");
    }

    std::filesystem::create_directories(root / "existing");
    const auto hard_link_source = root / "hard-link-source.txt";
    const auto hard_link_alias = root / "hard-link-alias.txt";
    const auto symbolic_link = root / "symbolic-link.txt";
    {
        std::ofstream output(root / "existing" / "before.txt");
        output << "linux registry policy test";
    }
    {
        std::ofstream output(hard_link_source);
        output << "linux hard link source";
    }
    std::filesystem::create_hard_link(hard_link_source, hard_link_alias);
    std::filesystem::create_symlink(
        root / "existing" / "before.txt",
        symbolic_link);

    try
    {
        const auto permissions_before = std::filesystem::status(root).permissions();

        const auto first = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, true);
        require(first.permissions.size() == 1, "Linux first registration result size mismatch");
        require(!first.final_error, "Linux first registration returned a final OS error");
        require(!first.permissions[0].reused, "Linux refresh=true must not report reuse");
        require(first.permissions[0].sid.empty(), "Linux registry must not expose a Windows SID");
        require(
            first.permissions[0].capability_name.starts_with(L"HomegrowphHarnessFilesystem"),
            "Linux filesystem policy identity signature mismatch");
        require(
            std::filesystem::exists(state_path),
            "Linux durable sandbox registry state file was not created");
        require(
            std::filesystem::status(root).permissions() == permissions_before,
            "Linux registry unexpectedly changed host filesystem permissions");
        require(
            has_path_error(first, hard_link_source, ENOTSUP),
            "Linux hard-link source was not reported");
        require(
            has_path_error(first, hard_link_alias, ENOTSUP),
            "Linux hard-link alias was not reported");
        require(
            has_path_error(first, symbolic_link, ENOTSUP),
            "Linux symbolic link was not reported");

        std::ifstream state_before_stream(state_path, std::ios::binary);
        const std::string state_before(
            std::istreambuf_iterator<char>(state_before_stream),
            std::istreambuf_iterator<char>());

        const auto second = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, false);
        require(second.permissions.size() == 1, "Linux reuse result size mismatch");
        require(second.permissions[0].reused, "Linux refresh=false must report reuse");
        require(
            second.permissions[0].capability_name == first.permissions[0].capability_name,
            "Linux policy identity was not stable");

        std::ifstream state_after_stream(state_path, std::ios::binary);
        const std::string state_after(
            std::istreambuf_iterator<char>(state_after_stream),
            std::istreambuf_iterator<char>());
        require(
            state_after == state_before,
            "Linux refresh=false unexpectedly mutated durable registry state");

        const auto missing_modify = sandbox::registry({
            {root, sandbox::permission::read_write},
        }, false);
        require(
            has_path_error(missing_modify, root, ENOENT),
            "Linux reuse of an unregistered policy did not preserve ENOENT");

        const auto modify = sandbox::registry({
            {root, sandbox::permission::read_write},
        }, true);
        require(modify.permissions.size() == 1, "Linux modify refresh result size mismatch");
        require(!modify.permissions[0].reused, "Linux modify refresh=true reported reuse");
        require(
            modify.permissions[0].capability_name != first.permissions[0].capability_name,
            "Linux read-only and read-write identities must be distinct");
        require(
            std::filesystem::status(root).permissions() == permissions_before,
            "Linux read-write registration changed host filesystem permissions");

        const auto released = sandbox::release(root);
        require(!released.final_error, "Linux release(path) returned a final OS error");
        require(released.path_errors.empty(), "Linux release(path) returned a path OS error");
        require(
            std::filesystem::status(root).permissions() == permissions_before,
            "Linux release(path) changed host filesystem permissions");

        const auto released_read_only_reuse = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, false);
        require(
            has_path_error(released_read_only_reuse, root, ENOENT),
            "Linux release(path) left read-only durable state behind");
        const auto released_read_write_reuse = sandbox::registry({
            {root, sandbox::permission::read_write},
        }, false);
        require(
            has_path_error(released_read_write_reuse, root, ENOENT),
            "Linux release(path) left read-write durable state behind");

        const auto repeated_release = sandbox::release(root);
        require(!repeated_release.final_error, "Linux repeated release(path) failed");
        require(repeated_release.path_errors.empty(), "Linux repeated release(path) returned a path error");

        const auto refreshed_for_release_all = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, true);
        require(
            refreshed_for_release_all.permissions.size() == 1,
            "Linux release_all setup registration failed");
        const auto released_all = sandbox::release_all();
        require(!released_all.final_error, "Linux release_all() returned a final OS error");
        require(released_all.path_errors.empty(), "Linux release_all() returned a path OS error");
        require(
            std::filesystem::status(root).permissions() == permissions_before,
            "Linux release_all() changed host filesystem permissions");
        const auto released_all_reuse = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, false);
        require(
            has_path_error(released_all_reuse, root, ENOENT),
            "Linux release_all() left durable state behind");
        const auto repeated_release_all = sandbox::release_all();
        require(!repeated_release_all.final_error, "Linux repeated release_all() failed");
        require(
            repeated_release_all.path_errors.empty(),
            "Linux repeated release_all() returned a path error");

        std::filesystem::remove(state_path);
        const auto missing_state = sandbox::registry({
            {root, sandbox::permission::read_only},
        }, false);
        require(
            missing_state.final_error.value() == ENOENT,
            "Linux missing registry state did not preserve OS ENOENT");

        std::filesystem::remove_all(root);
        std::filesystem::remove(state_path, cleanup_error);
        std::filesystem::remove(lock_path, cleanup_error);
        ::unsetenv("HOMEGROWPH_SANDBOX_REGISTRY_STATE");
        std::cout << "sandbox registry Linux tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::filesystem::remove_all(root, cleanup_error);
        std::filesystem::remove(state_path, cleanup_error);
        std::filesystem::remove(lock_path, cleanup_error);
        ::unsetenv("HOMEGROWPH_SANDBOX_REGISTRY_STATE");
        std::cerr << "sandbox registry Linux test failed: "
                  << exception.what()
                  << '\n';
        return 1;
    }
#else
    std::cout << "sandbox registry test skipped on unsupported platform\n";
    return 0;
#endif
}
