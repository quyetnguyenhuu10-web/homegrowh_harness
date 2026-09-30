#include "filesystem.h"

#include "../error_schema.h"
#include "identity.h"
#include "state.h"

#include <cerrno>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace sandbox::detail::filesystem::linux
{
    namespace
    {
        bool inspect_registry_path(
            const std::filesystem::path& path,
            std::vector<registry_path_error>& path_errors)
        {
            std::error_code error;
            const auto status = std::filesystem::symlink_status(path, error);
            if (error)
            {
                path_errors.push_back({
                    path,
                    sandbox::detail::make_system_error(
                        "std::filesystem::symlink_status",
                        error,
                        path),
                });
                return false;
            }

            if (std::filesystem::is_symlink(status))
            {
                path_errors.push_back({
                    path,
                    sandbox::detail::make_system_error(
                        "inspect_registry_path",
                        std::error_code(ENOTSUP, std::generic_category()),
                        path),
                });
                return false;
            }

            if (std::filesystem::is_regular_file(status))
            {
                const auto links = std::filesystem::hard_link_count(path, error);
                if (error)
                {
                    path_errors.push_back({
                        path,
                        sandbox::detail::make_system_error(
                            "std::filesystem::hard_link_count",
                            error,
                            path),
                    });
                    return false;
                }
                if (links > 1)
                {
                    path_errors.push_back({
                        path,
                        sandbox::detail::make_system_error(
                            "inspect_registry_path",
                            std::error_code(ENOTSUP, std::generic_category()),
                            path),
                    });
                    return false;
                }
            }
            else if (!std::filesystem::is_directory(status))
            {
                path_errors.push_back({
                    path,
                    sandbox::detail::make_system_error(
                        "inspect_registry_path",
                        std::error_code(ENOTSUP, std::generic_category()),
                        path),
                });
                return false;
            }
            return true;
        }

        void inspect_tree(
            const std::filesystem::path& root,
            std::vector<registry_path_error>& path_errors)
        {
            std::error_code iterator_error;
            std::filesystem::recursive_directory_iterator iterator(
                root,
                iterator_error);
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
                return;
            }

            while (iterator != end)
            {
                const auto path = iterator->path();
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
        }

        std::optional<registered_permission> refresh_one(
            const registry_request& request,
            registry_state& state,
            std::vector<registry_path_error>& path_errors)
        {
            if (!inspect_registry_path(request.path, path_errors))
                return std::nullopt;

            const std::filesystem::path canonical_path =
                canonical_existing_path(request.path);
            const std::wstring identity = policy_identity(
                canonical_path,
                request.access);

            registry_entry* existing = find_registry_entry(
                state,
                canonical_path,
                request.access);
            registry_entry updated{
                canonical_path.native(),
                std::string(permission_name(request.access)),
                identity,
            };

            if (existing != nullptr)
                *existing = updated;
            else
                state.entries.push_back(std::move(updated));

            inspect_tree(canonical_path, path_errors);

            /*
             * Linux registration stores Landlock policy metadata only. It does
             * not chmod/chown/setfacl the host filesystem. Actual restriction
             * is applied later to the sandbox process with Landlock.
             */
            return registered_permission{
                canonical_path,
                request.access,
                identity,
                {},
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
            registry_entry* existing = find_registry_entry(
                state,
                canonical_path,
                request.access);
            if (existing == nullptr)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "reuse_filesystem_policy",
                        "not_registered",
                        "sandbox filesystem policy is not registered",
                        {{{"path", sandbox::detail::error_path_text(canonical_path)},
                          {"code", ENOENT},
                          {"category", std::generic_category().name()}}}));
            }

            const std::wstring expected = policy_identity(
                canonical_path,
                request.access);
            if (existing->policy_name != expected)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "reuse_filesystem_policy",
                        "identity_mismatch",
                        "sandbox registry state policy identity mismatch",
                        {{{"path", sandbox::detail::error_path_text(canonical_path)}}}));
            }

            return registered_permission{
                canonical_path,
                request.access,
                existing->policy_name,
                {},
                true,
            };
        }
    }

    registry_result refresh_permissions(
        const std::vector<registry_request>& requests)
    {
        if (requests.empty())
            return registry_result{};

        const std::filesystem::path state_path = registry_state_path();
        registry_state_lock lock(state_path, true);
        registry_state state = load_registry_state(state_path, true);

        registry_result result;
        result.permissions.reserve(requests.size());
        for (const registry_request& request : requests)
        {
            try
            {
                auto permission = refresh_one(
                    request,
                    state,
                    result.path_errors);
                if (permission)
                    result.permissions.push_back(std::move(*permission));
            }
            catch (...)
            {
                result.path_errors.push_back({
                    request.path,
                    sandbox::detail::capture_exception(
                        "refresh_filesystem_policy", std::current_exception(),
                        {{"path", sandbox::detail::error_path_text(request.path)}}),
                });
            }
        }

        try
        {
            save_registry_state(state_path, state);
        }
        catch (...)
        {
            result.final_error = sandbox::detail::capture_exception(
                "save_registry_state", std::current_exception(),
                {{"path", sandbox::detail::error_path_text(state_path)}});
        }
        return result;
    }

    registry_result reuse_permissions(
        const std::vector<registry_request>& requests)
    {
        if (requests.empty())
            return registry_result{};

        const std::filesystem::path state_path = registry_state_path();
        registry_state_lock lock(state_path, false);
        registry_state state = load_registry_state(state_path, false);

        registry_result result;
        result.permissions.reserve(requests.size());
        for (const registry_request& request : requests)
        {
            try
            {
                auto permission = reuse_one(
                    request,
                    state,
                    result.path_errors);
                if (permission)
                    result.permissions.push_back(std::move(*permission));
            }
            catch (...)
            {
                result.path_errors.push_back({
                    request.path,
                    sandbox::detail::capture_exception(
                        "reuse_filesystem_policy", std::current_exception(),
                        {{"path", sandbox::detail::error_path_text(request.path)}}),
                });
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
        catch (...)
        {
            result.path_errors.push_back({
                path,
                sandbox::detail::capture_exception(
                    "canonical_existing_path", std::current_exception(),
                        {{"path", sandbox::detail::error_path_text(path)}}),
            });
            return result;
        }
        const std::string wanted_path = canonical_path.native();

        const std::filesystem::path state_path = registry_state_path();
        registry_state_lock lock(state_path, true);
        registry_state state = load_registry_state(state_path, true);

        const auto old_size = state.entries.size();
        std::erase_if(
            state.entries,
            [&](const registry_entry& entry) {
                return entry.canonical_path == wanted_path;
            });
        if (state.entries.size() == old_size)
            return {};

        try
        {
            save_registry_state(state_path, state);
        }
        catch (...)
        {
            result.final_error = sandbox::detail::capture_exception(
                "save_registry_state", std::current_exception(),
                {{"path", sandbox::detail::error_path_text(state_path)}});
        }
        return result;
    }

    release_result release_all_permissions()
    {
        const std::filesystem::path state_path = registry_state_path();
        registry_state_lock lock(state_path, true);
        registry_state state = load_registry_state(state_path, true);
        if (state.entries.empty())
            return {};

        state.entries.clear();
        release_result result;
        try
        {
            save_registry_state(state_path, state);
        }
        catch (...)
        {
            result.final_error = sandbox::detail::capture_exception(
                "save_registry_state", std::current_exception(),
                {{"path", sandbox::detail::error_path_text(state_path)}});
        }
        return result;
    }
}
