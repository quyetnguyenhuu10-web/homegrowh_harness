#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "landlock.h"

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#if __has_include(<linux/landlock.h>)
#include <linux/landlock.h>
#define HOMEGROWPH_HAS_LANDLOCK_HEADERS 1
#else
#define HOMEGROWPH_HAS_LANDLOCK_HEADERS 0
#endif

#if HOMEGROWPH_HAS_LANDLOCK_HEADERS && defined(__NR_landlock_create_ruleset) && defined(__NR_landlock_add_rule) && defined(__NR_landlock_restrict_self)
#define HOMEGROWPH_HAS_LANDLOCK 1
#else
#define HOMEGROWPH_HAS_LANDLOCK 0
#endif

namespace sandbox::detail::process::linux
{
#if HOMEGROWPH_HAS_LANDLOCK
    namespace
    {
        int create_ruleset(
            const landlock_ruleset_attr* attributes,
            std::size_t size,
            std::uint32_t flags) noexcept
        {
            return static_cast<int>(syscall(
                __NR_landlock_create_ruleset,
                attributes,
                size,
                flags));
        }

        int add_rule(
            int ruleset_fd,
            const landlock_path_beneath_attr* rule) noexcept
        {
            return static_cast<int>(syscall(
                __NR_landlock_add_rule,
                ruleset_fd,
                LANDLOCK_RULE_PATH_BENEATH,
                rule,
                0));
        }

        int restrict_self(int ruleset_fd) noexcept
        {
            return static_cast<int>(syscall(
                __NR_landlock_restrict_self,
                ruleset_fd,
                0));
        }

        std::uint64_t handled_access_for_abi(int abi) noexcept
        {
            std::uint64_t access =
                LANDLOCK_ACCESS_FS_EXECUTE
                | LANDLOCK_ACCESS_FS_WRITE_FILE
                | LANDLOCK_ACCESS_FS_READ_FILE
                | LANDLOCK_ACCESS_FS_READ_DIR
                | LANDLOCK_ACCESS_FS_REMOVE_DIR
                | LANDLOCK_ACCESS_FS_REMOVE_FILE
                | LANDLOCK_ACCESS_FS_MAKE_CHAR
                | LANDLOCK_ACCESS_FS_MAKE_DIR
                | LANDLOCK_ACCESS_FS_MAKE_REG
                | LANDLOCK_ACCESS_FS_MAKE_SOCK
                | LANDLOCK_ACCESS_FS_MAKE_FIFO
                | LANDLOCK_ACCESS_FS_MAKE_BLOCK
                | LANDLOCK_ACCESS_FS_MAKE_SYM;
#ifdef LANDLOCK_ACCESS_FS_REFER
            if (abi >= 2)
                access |= LANDLOCK_ACCESS_FS_REFER;
#endif
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
            if (abi >= 3)
                access |= LANDLOCK_ACCESS_FS_TRUNCATE;
#endif
            return access;
        }

        std::uint64_t read_only_access(
            std::uint64_t handled,
            bool directory) noexcept
        {
            std::uint64_t access = handled & (
                LANDLOCK_ACCESS_FS_EXECUTE
                | LANDLOCK_ACCESS_FS_READ_FILE);
            if (directory)
                access |= handled & LANDLOCK_ACCESS_FS_READ_DIR;
            return access;
        }

        std::uint64_t read_write_access(
            std::uint64_t handled,
            bool directory) noexcept
        {
            if (directory)
                return handled;

            std::uint64_t access = handled & (
                LANDLOCK_ACCESS_FS_EXECUTE
                | LANDLOCK_ACCESS_FS_WRITE_FILE
                | LANDLOCK_ACCESS_FS_READ_FILE);
#ifdef LANDLOCK_ACCESS_FS_TRUNCATE
            access |= handled & LANDLOCK_ACCESS_FS_TRUNCATE;
#endif
            return access;
        }

        bool add_path_rule(
            int ruleset_fd,
            const std::filesystem::path& path,
            std::uint64_t access,
            bool optional) noexcept
        {
            const int fd = open(path.c_str(), O_PATH | O_CLOEXEC);
            if (fd < 0)
            {
                if (optional && errno == ENOENT)
                    return true;
                return false;
            }

            landlock_path_beneath_attr rule{};
            rule.allowed_access = access;
            rule.parent_fd = fd;
            const int result = add_rule(ruleset_fd, &rule);
            const int saved_errno = errno;
            close(fd);
            errno = saved_errno;
            return result == 0;
        }

        bool is_directory(const std::filesystem::path& path) noexcept
        {
            struct stat status{};
            if (stat(path.c_str(), &status) != 0)
                return false;
            return S_ISDIR(status.st_mode);
        }
    }
#endif

    std::error_code apply_landlock(
        const process_request& request,
        const registry_result& registry) noexcept
    {
#if !HOMEGROWPH_HAS_LANDLOCK
        return std::error_code(ENOSYS, std::generic_category());
#else
        errno = 0;
        const int abi = create_ruleset(
            nullptr,
            0,
            LANDLOCK_CREATE_RULESET_VERSION);
        if (abi < 1)
            return std::error_code(errno == 0 ? ENOSYS : errno, std::generic_category());

        const std::uint64_t handled = handled_access_for_abi(abi);
        landlock_ruleset_attr ruleset_attributes{};
        ruleset_attributes.handled_access_fs = handled;
        const int ruleset_fd = create_ruleset(
            &ruleset_attributes,
            sizeof(ruleset_attributes),
            0);
        if (ruleset_fd < 0)
            return std::error_code(errno, std::generic_category());

        const auto fail = [ruleset_fd]() noexcept {
            const int error = errno;
            close(ruleset_fd);
            return std::error_code(error, std::generic_category());
        };

        const std::uint64_t readonly_directory = read_only_access(handled, true);
        const std::uint64_t readonly_file = read_only_access(handled, false);
        static constexpr const char* system_paths[] = {
            "/usr",
            "/bin",
            "/sbin",
            "/lib",
            "/lib64",
            "/etc",
            "/dev",
            "/proc",
        };
        for (const char* path : system_paths)
        {
            if (!add_path_rule(ruleset_fd, path, readonly_directory, true))
                return fail();
        }

        if (!add_path_rule(
                ruleset_fd,
                request.executable,
                readonly_file,
                false))
        {
            return fail();
        }

        for (const registered_permission& registered : registry.permissions)
        {
            const bool directory = is_directory(registered.path);
            const std::uint64_t access = registered.access == permission::read_only
                ? read_only_access(handled, directory)
                : read_write_access(handled, directory);
            if (!add_path_rule(
                    ruleset_fd,
                    registered.path,
                    access,
                    false))
            {
                return fail();
            }
        }

        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
            return fail();
        if (restrict_self(ruleset_fd) != 0)
            return fail();

        close(ruleset_fd);
        return {};
#endif
    }
}
