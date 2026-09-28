#include "state.h"

#include "identity.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace sandbox::detail::filesystem::linux
{
    namespace
    {
        [[noreturn]] void throw_errno(const char* action)
        {
            throw std::system_error(errno, std::generic_category(), action);
        }

        std::string narrow(std::wstring_view input)
        {
            std::string output;
            output.reserve(input.size());
            for (const wchar_t value : input)
            {
                if (value < 0 || value > 0x7f)
                    throw std::runtime_error("Linux policy identity is not ASCII");
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
                throw std::system_error(error, "could not create sandbox registry state directory");
        }

        std::string read_all(const std::filesystem::path& path, bool missing_is_empty)
        {
            const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0)
            {
                if (missing_is_empty && errno == ENOENT)
                    return {};
                throw_errno("open(sandbox registry state)");
            }

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
                const int error = errno;
                ::close(fd);
                errno = error;
                throw_errno("read(sandbox registry state)");
            }

            if (::close(fd) != 0)
                throw_errno("close(sandbox registry state)");
            return content;
        }

        void write_all(int fd, std::string_view data)
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
                    errno = EIO;
                throw_errno("write(sandbox registry state)");
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
            throw_errno("open(sandbox registry lock)");

        if (::flock(fd_, create ? LOCK_EX : LOCK_SH) != 0)
        {
            const int error = errno;
            ::close(fd_);
            fd_ = -1;
            errno = error;
            throw_errno("flock(sandbox registry lock)");
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
            throw std::runtime_error(
                "HOME or XDG_STATE_HOME is required for durable sandbox registry state");
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
            throw std::runtime_error("sandbox registry state header is invalid");
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
                throw std::runtime_error("sandbox registry state entry is invalid");
            }

            if (access == "read_modify")
                access = "read_write";

            if (canonical_path.empty()
                || policy_name.empty()
                || (access != "read_only" && access != "read_write"))
            {
                throw std::runtime_error("sandbox registry state entry is incomplete");
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
            throw_errno("open(sandbox registry state temp)");

        try
        {
            const std::string serialized = output.str();
            write_all(fd, serialized);
            if (::fsync(fd) != 0)
                throw_errno("fsync(sandbox registry state temp)");
            const int close_result = ::close(fd);
            fd = -1;
            if (close_result != 0)
                throw_errno("close(sandbox registry state temp)");

            if (::rename(temporary.c_str(), path.c_str()) != 0)
                throw_errno("rename(sandbox registry state)");
        }
        catch (...)
        {
            const int saved_errno = errno;
            if (fd >= 0)
                ::close(fd);
            ::unlink(temporary.c_str());
            errno = saved_errno;
            throw;
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
