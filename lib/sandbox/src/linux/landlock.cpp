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
#include <utility>

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

        landlock_error add_path_rule(
            int ruleset_fd,
            const std::filesystem::path& path,
            std::uint64_t access,
            std::size_t permission_index) noexcept
        {
            const int fd = open(path.c_str(), O_PATH | O_CLOEXEC);
            if (fd < 0)
            {
                return {
                    errno,
                    landlock_error_stage::open_path,
                    permission_index,
                };
            }

            landlock_path_beneath_attr rule{};
            rule.allowed_access = access;
            rule.parent_fd = fd;
            const int result = add_rule(ruleset_fd, &rule);
            const int saved_errno = errno;
            const int close_result = close(fd);
            const int close_error = close_result == 0 ? 0 : errno;

            if (result != 0)
            {
                return {
                    saved_errno,
                    landlock_error_stage::add_rule,
                    permission_index,
                    close_error,
                    close_error == 0
                        ? landlock_error_stage::none
                        : landlock_error_stage::close_path,
                };
            }
            if (close_error != 0)
            {
                return {
                    close_error,
                    landlock_error_stage::close_path,
                    permission_index,
                };
            }
            return {};
        }
    }
#endif

    landlock_error apply_landlock(const registry_result& registry) noexcept
    {
#if !HOMEGROWPH_HAS_LANDLOCK
        return {
            0,
            landlock_error_stage::unsupported,
        };
#else
        errno = 0;
        const int abi = create_ruleset(
            nullptr,
            0,
            LANDLOCK_CREATE_RULESET_VERSION);
        if (abi < 1)
        {
            landlock_error failure;
            failure.code = errno;
            failure.stage = landlock_error_stage::query_abi;
            failure.native_result = abi;
            return failure;
        }

        const std::uint64_t handled = handled_access_for_abi(abi);
        landlock_ruleset_attr ruleset_attributes{};
        ruleset_attributes.handled_access_fs = handled;
        const int ruleset_fd = create_ruleset(
            &ruleset_attributes,
            sizeof(ruleset_attributes),
            0);
        if (ruleset_fd < 0)
        {
            return {
                errno,
                landlock_error_stage::create_ruleset,
            };
        }

        const auto finish_failure = [ruleset_fd](landlock_error failure) noexcept {
            if (close(ruleset_fd) != 0)
            {
                failure.cleanup_code = errno;
                failure.cleanup_stage = landlock_error_stage::close_ruleset;
            }
            return failure;
        };

        for (std::size_t index = 0; index < registry.permissions.size(); ++index)
        {
            const registered_permission& registered = registry.permissions[index];
            struct stat status{};
            if (stat(registered.path.c_str(), &status) != 0)
            {
                return finish_failure({
                    errno,
                    landlock_error_stage::inspect_path,
                    index,
                });
            }
            const bool directory = S_ISDIR(status.st_mode);
            const std::uint64_t access = registered.access == permission::read_only
                ? read_only_access(handled, directory)
                : read_write_access(handled, directory);
            landlock_error path_error = add_path_rule(
                ruleset_fd,
                registered.path,
                access,
                index);
            if (path_error)
            {
                return finish_failure(std::move(path_error));
            }
        }

        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
        {
            return finish_failure({
                errno,
                landlock_error_stage::set_no_new_privileges,
            });
        }
        if (restrict_self(ruleset_fd) != 0)
        {
            return finish_failure({
                errno,
                landlock_error_stage::restrict_self,
            });
        }

        if (close(ruleset_fd) != 0)
        {
            return {
                errno,
                landlock_error_stage::close_ruleset,
            };
        }
        return {};
#endif
    }
}
