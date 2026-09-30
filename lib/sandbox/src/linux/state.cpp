#include "state.h"

#include "../error_schema.h"
#include "identity.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace sandbox::detail::filesystem::linux
{
    namespace
    {
        [[noreturn]] void throw_errno(
            const char* action,
            const std::filesystem::path& path)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_system_error(
                    action,
                    std::error_code(errno, std::generic_category()),
                    path));
        }

        std::string narrow(std::wstring_view input)
        {
            std::string output;
            output.reserve(input.size());
            for (const wchar_t value : input)
            {
                if (value < 0 || value > 0x7f)
                {
                    sandbox::detail::throw_error(
                        sandbox::detail::make_error(
                            "encode_policy_identity",
                            "encoding_error",
                            "Linux policy identity is not ASCII",
                            {{{"code_point", static_cast<std::uint32_t>(value)}}}));
                }
                output.push_back(static_cast<char>(value));
            }
            return output;
        }

        std::wstring wide_ascii(std::string_view input)
        {
            std::wstring output;
            output.reserve(input.size());
            for (const unsigned char value : input)
                output.push_back(static_cast<wchar_t>(value));
            return output;
        }

        std::filesystem::path lock_path(const std::filesystem::path& state_path)
        {
            std::filesystem::path path = state_path;
            path += ".lock";
            return path;
        }

        void ensure_parent(const std::filesystem::path& path)
        {
            const auto parent = path.parent_path();
            if (parent.empty())
                return;

            std::error_code error;
            std::filesystem::create_directories(parent, error);
            if (error)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_system_error(
                        "std::filesystem::create_directories",
                        error,
                        parent));
            }
        }

        class scoped_descriptor final
        {
        public:
            scoped_descriptor(int descriptor, int& cleanup_error) noexcept
                : descriptor_(descriptor), cleanup_error_(cleanup_error)
            {
            }

            scoped_descriptor(const scoped_descriptor&) = delete;
            scoped_descriptor& operator=(const scoped_descriptor&) = delete;

            ~scoped_descriptor()
            {
                const int error = close();
                if (error != 0)
                    cleanup_error_ = error;
            }

            int close() noexcept
            {
                if (descriptor_ < 0)
                    return 0;
                const int descriptor = std::exchange(descriptor_, -1);
                if (::close(descriptor) != 0)
                    return errno;
                return 0;
            }

        private:
            int descriptor_;
            int& cleanup_error_;
        };

        std::string read_all(const std::filesystem::path& path, bool missing_is_empty)
        {
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0)
            {
                if (missing_is_empty && errno == ENOENT)
                    return {};
                throw_errno("open(sandbox registry state)", path);
            }

            int cleanup_error = 0;
            try
            {
                scoped_descriptor descriptor(fd, cleanup_error);
                std::string content;
                std::array<char, 8192> buffer{};
                for (;;)
                {
                    const ssize_t bytes = ::read(fd, buffer.data(), buffer.size());
                    if (bytes > 0)
                    {
                        content.append(buffer.data(), static_cast<std::size_t>(bytes));
                        continue;
                    }
                    if (bytes == 0)
                        break;
                    if (errno == EINTR)
                        continue;
                    throw_errno("read(sandbox registry state)", path);
                }
                const int error = descriptor.close();
                if (error != 0)
                {
                    sandbox::detail::throw_error(sandbox::detail::make_system_error(
                        "close(sandbox registry state)",
                        std::error_code(error, std::generic_category()), path));
                }
                return content;
            }
            catch (...)
            {
                Error primary = sandbox::detail::capture_exception(
                    "read_registry_state", std::current_exception(),
                    {{"path", sandbox::detail::error_path_text(path)}});
                if (cleanup_error == 0)
                    sandbox::detail::throw_error(std::move(primary));
                std::vector<Error> causes;
                causes.push_back(std::move(primary));
                causes.push_back(sandbox::detail::make_system_error(
                    "close(sandbox registry state)",
                    std::error_code(cleanup_error, std::generic_category()), path));
                sandbox::detail::throw_error(sandbox::detail::make_error(
                    "read_registry_state", "dependency_error",
                    "Reading registry state and closing its descriptor both failed",
                    nullptr, std::move(causes)));
            }
        }

        void write_all(
            int fd,
            std::string_view data,
            const std::filesystem::path& path)
        {
            std::size_t offset = 0;
            while (offset < data.size())
            {
                const ssize_t bytes = ::write(
                    fd,
                    data.data() + offset,
                    data.size() - offset);
                if (bytes > 0)
                {
                    offset += static_cast<std::size_t>(bytes);
                    continue;
                }
                if (bytes < 0 && errno == EINTR)
                    continue;
                if (bytes == 0)
                {
                    sandbox::detail::throw_error(sandbox::detail::make_error(
                        "write(sandbox registry state)", "io_error",
                        "Write completed without making progress",
                        {{"api", "write"},
                         {"path", sandbox::detail::error_path_text(path)},
                         {"offset", offset},
                         {"requested", data.size() - offset},
                         {"written", 0}}));
                }
                throw_errno("write(sandbox registry state)", path);
            }
        }
    }

    registry_state_lock::registry_state_lock(
        const std::filesystem::path& state_path,
        bool create)
    {
        if (create)
            ensure_parent(state_path);

        const auto path = lock_path(state_path);
        const int flags = O_CLOEXEC
            | (create ? (O_RDWR | O_CREAT) : O_RDONLY);
        fd_ = ::open(path.c_str(), flags, 0600);
        if (fd_ < 0)
            throw_errno("open(sandbox registry lock)", path);

        if (::flock(fd_, create ? LOCK_EX : LOCK_SH) != 0)
        {
            const int error = errno;
            Error primary = sandbox::detail::make_system_error(
                "flock(sandbox registry lock)",
                std::error_code(error, std::generic_category()),
                path);
            const int close_result = ::close(fd_);
            const int close_error = close_result == 0 ? 0 : errno;
            fd_ = -1;
            if (close_error != 0)
            {
                Error cleanup = sandbox::detail::make_system_error(
                    "close(sandbox registry lock)",
                    std::error_code(close_error, std::generic_category()),
                    path);
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "lock_registry_state",
                        "operation_failed",
                        "locking sandbox registry state and cleanup both failed",
                        nullptr,
                        {std::move(primary), std::move(cleanup)}));
            }
            sandbox::detail::throw_error(std::move(primary));
        }
    }

    registry_state_lock::~registry_state_lock()
    {
        if (fd_ < 0)
            return;
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }

    std::filesystem::path registry_state_path()
    {
        if (const char* override_path = std::getenv("HOMEGROWPH_SANDBOX_REGISTRY_STATE"))
        {
            if (*override_path != '\0')
                return std::filesystem::path(override_path);
        }

        if (const char* xdg_state_home = std::getenv("XDG_STATE_HOME"))
        {
            if (*xdg_state_home != '\0')
            {
                return std::filesystem::path(xdg_state_home)
                    / std::string(capability_signature)
                    / "sandbox-registry.state";
            }
        }

        const char* home = std::getenv("HOME");
        if (home == nullptr || *home == '\0')
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "registry_state_path",
                    "environment_error",
                    "HOME or XDG_STATE_HOME is required for durable sandbox registry state",
                    {{{"variables", {"HOME", "XDG_STATE_HOME"}}}}));
        }

        return std::filesystem::path(home)
            / ".local"
            / "state"
            / std::string(capability_signature)
            / "sandbox-registry.state";
    }

    registry_state load_registry_state(
        const std::filesystem::path& path,
        bool missing_is_empty)
    {
        const std::string content = read_all(path, missing_is_empty);
        if (content.empty())
            return {};

        std::istringstream input(content);
        std::string signature;
        int schema = 0;
        if (!(input >> std::quoted(signature) >> schema)
            || signature != std::string(capability_signature)
            || schema != registry_schema_version)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "load_registry_state",
                    "invalid_state",
                    "sandbox registry state header is invalid",
                    {{{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())}}}));
        }

        registry_state state;
        while (input >> std::ws && input.peek() != std::char_traits<char>::eof())
        {
            std::string canonical_path;
            std::string access;
            std::string policy_name;
            if (!(input
                    >> std::quoted(canonical_path)
                    >> std::quoted(access)
                    >> std::quoted(policy_name)))
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "load_registry_state",
                        "invalid_state",
                        "sandbox registry state entry is invalid",
                        {{{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())}}}));
            }

            if (access == "read_modify")
                access = "read_write";

            if (canonical_path.empty()
                || policy_name.empty()
                || (access != "read_only" && access != "read_write"))
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "load_registry_state",
                        "invalid_state",
                        "sandbox registry state entry is incomplete",
                        {{{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())},
                          {"canonical_path", canonical_path},
                          {"access", access},
                          {"policy_name", policy_name}}}));
            }

            state.entries.push_back({
                canonical_path,
                access,
                wide_ascii(policy_name),
            });
        }

        for (std::size_t left = 0; left < state.entries.size(); ++left)
        {
            for (std::size_t right = left + 1; right < state.entries.size(); ++right)
            {
                if (state.entries[left].canonical_path
                        == state.entries[right].canonical_path
                    && state.entries[left].access == state.entries[right].access)
                {
                    sandbox::detail::throw_error(
                        sandbox::detail::make_error(
                            "load_registry_state",
                            "invalid_state",
                            "sandbox registry state contains duplicate entries",
                            {{{"path", sandbox::detail::error_path_text(path)}, {"body_bytes", std::vector<unsigned char>(content.begin(), content.end())},
                              {"canonical_path", state.entries[left].canonical_path},
                              {"access", state.entries[left].access}}}));
                }
            }
        }
        return state;
    }

    void save_registry_state(
        const std::filesystem::path& path,
        const registry_state& state)
    {
        ensure_parent(path);

        std::ostringstream output;
        output
            << std::quoted(std::string(capability_signature))
            << ' '
            << registry_schema_version
            << '\n';
        for (const registry_entry& entry : state.entries)
        {
            output
                << std::quoted(entry.canonical_path) << ' '
                << std::quoted(entry.access) << ' '
                << std::quoted(narrow(entry.policy_name))
                << '\n';
        }

        std::filesystem::path temporary = path;
        temporary += "." + std::to_string(::getpid()) + ".tmp";

        int fd = ::open(
            temporary.c_str(),
            O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
            0600);
        if (fd < 0)
            throw_errno("open(sandbox registry state temp)", temporary);

        const auto cleanup_and_throw = [&](Error primary)
        {
            std::vector<Error> cleanup_errors;
            if (fd >= 0)
            {
                if (::close(fd) != 0)
                {
                    cleanup_errors.push_back(sandbox::detail::make_system_error(
                        "close(sandbox registry state temp cleanup)",
                        std::error_code(errno, std::generic_category()),
                        temporary));
                }
                fd = -1;
            }

            if (::unlink(temporary.c_str()) != 0 && errno != ENOENT)
            {
                cleanup_errors.push_back(sandbox::detail::make_system_error(
                    "unlink(sandbox registry state temp cleanup)",
                    std::error_code(errno, std::generic_category()),
                    temporary));
            }

            if (cleanup_errors.empty())
                sandbox::detail::throw_error(std::move(primary));

            std::vector<Error> causes;
            causes.reserve(1 + cleanup_errors.size());
            causes.push_back(std::move(primary));
            for (Error& cleanup : cleanup_errors)
                causes.push_back(std::move(cleanup));
            sandbox::detail::throw_error(
                sandbox::detail::make_error(
                    "save_registry_state",
                    "operation_failed",
                    "saving sandbox registry state and cleanup both failed",
                    {{{"path", sandbox::detail::error_path_text(path)},
                      {"temporary_path", sandbox::detail::error_path_text(temporary)}}},
                    std::move(causes)));
        };

        try
        {
            const std::string serialized = output.str();
            write_all(fd, serialized, temporary);
            if (::fsync(fd) != 0)
                throw_errno("fsync(sandbox registry state temp)", temporary);
            const int close_result = ::close(fd);
            fd = -1;
            if (close_result != 0)
                throw_errno("close(sandbox registry state temp)", temporary);

            if (::rename(temporary.c_str(), path.c_str()) != 0)
            {
                Error failure = sandbox::detail::make_system_error(
                    "rename(sandbox registry state)",
                    std::error_code(errno, std::generic_category()),
                    path);
                failure.data.push_back({
                    {"temporary_path", sandbox::detail::error_path_text(temporary)},
                });
                sandbox::detail::throw_error(std::move(failure));
            }
        }
        catch (...)
        {
            cleanup_and_throw(sandbox::detail::capture_exception(
                "save_registry_state", std::current_exception(),
                {{"path", sandbox::detail::error_path_text(path)}}));
        }
    }

    registry_entry* find_registry_entry(
        registry_state& state,
        const std::filesystem::path& canonical_path,
        permission access)
    {
        const std::string wanted_path = canonical_path.native();
        const std::string wanted_access(permission_name(access));
        for (registry_entry& entry : state.entries)
        {
            if (entry.canonical_path == wanted_path
                && entry.access == wanted_access)
            {
                return &entry;
            }
        }
        return nullptr;
    }
}
