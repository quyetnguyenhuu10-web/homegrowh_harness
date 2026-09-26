#include "windows_filesystem.h"

#include "windows_filesystem_acl.h"
#include "windows_filesystem_identity.h"
#include "windows_filesystem_state.h"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace sandbox::detail::filesystem::windows
{
    namespace
    {
        std::optional<registered_permission> refresh_one(
            const registry_request& request,
            registry_state& state,
            bool& state_changed,
            std::vector<registry_path_error>& path_errors)
        {
            if (!inspect_registry_path(request.path, path_errors))
                return std::nullopt;

            const std::filesystem::path canonical_path =
                canonical_existing_path(request.path);
            capability_identity identity = derive_capability_identity(
                canonical_path,
                request.access);

            registry_entry* existing = find_registry_entry(
                state,
                canonical_path,
                request.access);
            if (existing != nullptr)
            {
                if (existing->capability_name != identity.name
                    || existing->sid != identity.sid_string)
                {
                    throw std::runtime_error(
                        "sandbox registry state capability identity mismatch");
                }
                if (existing->tree_version > tree_acl_version)
                {
                    throw std::runtime_error(
                        "sandbox registry state tree ACL version is newer than this build");
                }
            }

            /*
             * Refresh is an explicit decision made by sandbox::registry(...).
             * Filesystem never promotes reuse into repair on its own.
             */
            const std::size_t error_count_before = path_errors.size();
            reconcile_tree(
                canonical_path,
                identity.sid(),
                request.access,
                path_errors);
            registry_entry updated{
                canonical_path.native(),
                std::string(permission_name(request.access)),
                identity.name,
                identity.sid_string,
                path_errors.size() == error_count_before,
                tree_acl_version,
            };
            if (existing != nullptr)
                *existing = std::move(updated);
            else
                state.entries.push_back(std::move(updated));
            state_changed = true;

            return registered_permission{
                canonical_path,
                request.access,
                std::move(identity.name),
                std::move(identity.sid_string),
                false,
            };
        }

        std::optional<registered_permission> reuse_one(
            const registry_request& request,
            registry_state& state,
            std::vector<registry_path_error>& path_errors)
        {
            if (!inspect_registry_path(request.path, path_errors))
                return std::nullopt;

            const std::filesystem::path canonical_path =
                canonical_existing_path(request.path);
            capability_identity identity = derive_capability_identity(
                canonical_path,
                request.access);
            registry_entry* existing = find_registry_entry(
                state,
                canonical_path,
                request.access);
            if (existing == nullptr)
            {
                throw std::system_error(
                    static_cast<int>(ERROR_FILE_NOT_FOUND),
                    std::system_category(),
                    "sandbox filesystem capability is not registered");
            }
            if (existing->capability_name != identity.name
                || existing->sid != identity.sid_string)
            {
                throw std::runtime_error(
                    "sandbox registry state capability identity mismatch");
            }

            /*
             * Reuse deliberately performs no ACL probe, repair, or state
             * mutation. The capability is deterministic. If the permission is
             * no longer usable, the real filesystem operation is allowed to
             * fail and surface the original operating-system error unchanged.
             */
            return registered_permission{
                canonical_path,
                request.access,
                existing->capability_name,
                existing->sid,
                true,
            };
        }

        bool same_registered_path(
            const registry_entry& entry,
            const std::filesystem::path& path)
        {
            return _wcsicmp(
                entry.canonical_path.c_str(),
                path.c_str()) == 0;
        }

        template <typename Predicate>
        bool release_matching_entries(
            registry_state& state,
            Predicate&& matches,
            release_result& result)
        {
            bool state_changed = false;
            auto iterator = state.entries.begin();
            while (iterator != state.entries.end())
            {
                if (!matches(*iterator))
                {
                    ++iterator;
                    continue;
                }

                const std::filesystem::path root(iterator->canonical_path);
                if (auto error = release_tree(root, iterator->sid))
                {
                    result.path_errors.push_back(std::move(*error));
                    ++iterator;
                    continue;
                }

                iterator = state.entries.erase(iterator);
                state_changed = true;
            }
            return state_changed;
        }
    }

    registry_result refresh_permissions(
        const std::vector<registry_request>& requests)
    {
        if (requests.empty())
            return registry_result{};

        registry_state_lock lock;
        const std::filesystem::path state_path = registry_state_path();
        registry_state state = load_registry_state(state_path);
        bool state_changed = false;

        registry_result result;
        result.permissions.reserve(requests.size());
        for (const registry_request& request : requests)
        {
            try
            {
                auto permission = refresh_one(
                    request,
                    state,
                    state_changed,
                    result.path_errors);
                if (permission)
                    result.permissions.push_back(std::move(*permission));
            }
            catch (const std::system_error& exception)
            {
                result.path_errors.push_back({request.path, exception.code()});
            }
        }

        if (state_changed)
        {
            try
            {
                save_registry_state(state_path, state);
            }
            catch (const std::system_error& exception)
            {
                result.final_error = exception.code();
            }
        }
        return result;
    }

    registry_result reuse_permissions(
        const std::vector<registry_request>& requests)
    {
        if (requests.empty())
            return registry_result{};

        registry_state_lock lock;
        const std::filesystem::path state_path = registry_state_path();
        registry_state state = load_registry_state(state_path);

        registry_result result;
        result.permissions.reserve(requests.size());
        for (const registry_request& request : requests)
        {
            try
            {
                auto permission = reuse_one(request, state, result.path_errors);
                if (permission)
                    result.permissions.push_back(std::move(*permission));
            }
            catch (const std::system_error& exception)
            {
                result.path_errors.push_back({request.path, exception.code()});
            }
        }
        return result;
    }

    release_result release_permissions(const std::filesystem::path& path)
    {
        release_result result;
        std::filesystem::path canonical_path;
        try
        {
            canonical_path = canonical_existing_path(path);
        }
        catch (const std::system_error& exception)
        {
            result.path_errors.push_back({path, exception.code()});
            return result;
        }

        registry_state_lock lock;
        const std::filesystem::path state_path = registry_state_path();
        registry_state state = load_registry_state(state_path);

        const bool state_changed = release_matching_entries(
            state,
            [&](const registry_entry& entry) {
                return same_registered_path(entry, canonical_path);
            },
            result);
        if (!state_changed)
            return result;

        try
        {
            save_registry_state(state_path, state);
        }
        catch (const std::system_error& exception)
        {
            result.final_error = exception.code();
        }
        return result;
    }

    release_result release_all_permissions()
    {
        registry_state_lock lock;
        const std::filesystem::path state_path = registry_state_path();
        registry_state state = load_registry_state(state_path);

        release_result result;
        const bool state_changed = release_matching_entries(
            state,
            [](const registry_entry&) {
                return true;
            },
            result);
        if (!state_changed)
            return result;

        try
        {
            save_registry_state(state_path, state);
        }
        catch (const std::system_error& exception)
        {
            result.final_error = exception.code();
        }
        return result;
    }
}
