#pragma once

#include "../api/registry.h"

#include <filesystem>
#include <string>
#include <vector>

namespace sandbox::detail::filesystem::linux
{
    inline constexpr int registry_schema_version = 1;

    struct registry_entry
    {
        std::string canonical_path;
        std::string access;
        std::wstring policy_name;
    };

    struct registry_state
    {
        std::vector<registry_entry> entries;
    };

    class registry_state_lock
    {
    public:
        registry_state_lock(
            const std::filesystem::path& state_path,
            bool create);
        registry_state_lock(const registry_state_lock&) = delete;
        registry_state_lock& operator=(const registry_state_lock&) = delete;
        ~registry_state_lock();

    private:
        int fd_ = -1;
    };

    std::filesystem::path registry_state_path();

    registry_state load_registry_state(
        const std::filesystem::path& path,
        bool missing_is_empty);

    void save_registry_state(
        const std::filesystem::path& path,
        const registry_state& state);

    registry_entry* find_registry_entry(
        registry_state& state,
        const std::filesystem::path& canonical_path,
        permission access);
}
