#pragma once

#include <registry.h>

#include <filesystem>
#include <string>
#include <vector>

namespace sandbox::detail::filesystem::windows
{
    inline constexpr int registry_schema_version = 1;
    inline constexpr int tree_acl_version = 1;

    struct registry_entry
    {
        std::wstring canonical_path;
        std::string access;
        std::wstring capability_name;
        std::wstring sid;
        bool acl_ready = false;
        int tree_version = 0;
    };

    struct registry_state
    {
        std::vector<registry_entry> entries;
    };

    class registry_state_lock
    {
    public:
        registry_state_lock();
        registry_state_lock(const registry_state_lock&) = delete;
        registry_state_lock& operator=(const registry_state_lock&) = delete;
        ~registry_state_lock();

    private:
        void* handle_ = nullptr;
        bool locked_ = false;
    };

    std::filesystem::path registry_state_path();

    registry_state load_registry_state(const std::filesystem::path& path);

    void save_registry_state(
        const std::filesystem::path& path,
        const registry_state& state);

    registry_entry* find_registry_entry(
        registry_state& state,
        const std::filesystem::path& canonical_path,
        permission access);
}
