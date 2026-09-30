#include "filesystem.h"

#include "acl.h"
#include "identity.h"
#include "state.h"
#include "../error_schema.h"

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
        bool has_fatal_path_error(
            const std::vector<registry_path_error>& path_errors,
            std::size_t begin)
        {
            for (std::size_t index = begin; index < path_errors.size(); ++index)
            {
                if (path_errors[index].error.type != "unsupported_path")
                    return true;
            }
            return false;
        }

        bool commit_state(
            const std::filesystem::path& state_path,
            const registry_state& state,
            std::optional<Error>& final_error)
        {
            try
            {
                save_registry_state(state_path, state);
                return true;
            }
            catch (...)
            {
                final_error = sandbox::detail::capture_exception("filesystem", std::current_exception());
                return false;
            }
        }

        void merge_final_error(
            std::optional<Error>& destination,
            Error error,
            std::string operation,
            std::string message)
        {
            if (!destination)
            {
                destination = std::move(error);
                return;
            }
            std::vector<Error> causes;
            causes.reserve(2);
            causes.push_back(std::move(*destination));
            causes.push_back(std::move(error));
            destination = sandbox::detail::make_error(
                std::move(operation),
                "operation_failed",
                std::move(message),
                nullptr,
                std::move(causes));
        }

        std::optional<registered_permission> refresh_one(
            const registry_request& request,
            const std::filesystem::path& state_path,
            registry_state& state,
            std::vector<registry_path_error>& path_errors,
            std::optional<Error>& final_error)
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
                    sandbox::detail::throw_error(
                        sandbox::detail::make_error(
                            "refresh_filesystem_capability",
                            "identity_mismatch",
                            "sandbox registry state capability identity mismatch",
                            {{"path", sandbox::detail::error_path_text(canonical_path)}}));
                }
                if (existing->tree_version > tree_acl_version)
                {
                    sandbox::detail::throw_error(
                        sandbox::detail::make_error(
                            "refresh_filesystem_capability",
                            "unsupported_state_version",
                            "sandbox registry state tree ACL version is newer than this build",
                            {
                                {"path", sandbox::detail::error_path_text(canonical_path)},
                                {"stored_tree_version", existing->tree_version},
                                {"supported_tree_version", tree_acl_version},
                            }));
                }
            }

            /*
             * Persist a non-reusable transaction marker before touching ACLs.
             * A crash after this commit leaves enough durable information for
             * explicit refresh/release to recover safely.
             */
            registry_entry pending{
                canonical_path.native(),
                std::string(permission_name(request.access)),
                identity.name,
                identity.sid_string,
                false,
                tree_acl_version,
            };

            std::optional<registry_entry> previous;
            if (existing != nullptr)
            {
                previous = *existing;
                *existing = pending;
            }
            else
            {
                state.entries.push_back(pending);
                existing = &state.entries.back();
            }

            if (!commit_state(state_path, state, final_error))
            {
                if (previous.has_value())
                    *existing = std::move(*previous);
                else
                    state.entries.pop_back();
                return std::nullopt;
            }

            const std::size_t error_count_before = path_errors.size();
            reconcile_tree(
                canonical_path,
                identity.sid(),
                request.access,
                path_errors);

            if (has_fatal_path_error(path_errors, error_count_before))
                return std::nullopt;

            existing->acl_ready = true;
            if (!commit_state(state_path, state, final_error))
            {
                existing->acl_ready = false;
                return std::nullopt;
            }

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
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "reuse_filesystem_capability",
                        "not_registered",
                        "sandbox filesystem capability is not registered",
                        {
                            {"path", sandbox::detail::error_path_text(canonical_path)},
                            {"code", static_cast<int>(ERROR_FILE_NOT_FOUND)},
                            {"category", std::system_category().name()},
                        }));
            }
            if (existing->capability_name != identity.name
                || existing->sid != identity.sid_string)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "reuse_filesystem_capability",
                        "identity_mismatch",
                        "sandbox registry state capability identity mismatch",
                        {{"path", sandbox::detail::error_path_text(canonical_path)}}));
            }

            if (!existing->acl_ready)
            {
                sandbox::detail::throw_error(
                    sandbox::detail::make_error(
                        "reuse_filesystem_capability",
                        "incomplete_registration",
                        "sandbox filesystem capability transaction is incomplete",
                        {{"path", sandbox::detail::error_path_text(canonical_path)}}));
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
            const std::filesystem::path& state_path,
            registry_state& state,
            Predicate&& matches,
            release_result& result)
        {
            auto iterator = state.entries.begin();
            while (iterator != state.entries.end())
            {
                if (!matches(*iterator))
                {
                    ++iterator;
                    continue;
                }

                const std::filesystem::path root(iterator->canonical_path);

                if (iterator->acl_ready)
                {
                    iterator->acl_ready = false;
                    if (!commit_state(state_path, state, result.final_error))
                    {
                        iterator->acl_ready = true;
                        return false;
                    }
                }

                if (auto error = release_tree(root, iterator->sid))
                {
                    result.path_errors.push_back(std::move(*error));
                    ++iterator;
                    continue;
                }

                iterator = state.entries.erase(iterator);
                if (!commit_state(state_path, state, result.final_error))
                    return false;
            }
            return true;
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

        registry_result result;
        result.permissions.reserve(requests.size());
        for (const registry_request& request : requests)
        {
            try
            {
                auto permission = refresh_one(
                    request,
                    state_path,
                    state,
                    result.path_errors,
                    result.final_error);
                if (permission)
                    result.permissions.push_back(std::move(*permission));
                if (result.final_error)
                    break;
            }
            catch (...)
            {
                result.path_errors.push_back({request.path, sandbox::detail::capture_exception("filesystem", std::current_exception())});
            }
        }
        if (auto close_error = lock.close())
        {
            merge_final_error(
                result.final_error,
                std::move(*close_error),
                "refresh_permissions",
                "filesystem refresh failed and registry lock cleanup also failed");
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
            catch (...)
            {
                result.path_errors.push_back({request.path, sandbox::detail::capture_exception("filesystem", std::current_exception())});
            }
        }
        if (auto close_error = lock.close())
        {
            merge_final_error(
                result.final_error,
                std::move(*close_error),
                "reuse_permissions",
                "filesystem reuse failed and registry lock cleanup also failed");
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
            result.path_errors.push_back({path, sandbox::detail::capture_exception("filesystem", std::current_exception())});
            return result;
        }

        registry_state_lock lock;
        const std::filesystem::path state_path = registry_state_path();
        registry_state state = load_registry_state(state_path);

        release_matching_entries(
            state_path,
            state,
            [&](const registry_entry& entry) {
                return same_registered_path(entry, canonical_path);
            },
            result);
        if (auto close_error = lock.close())
        {
            merge_final_error(
                result.final_error,
                std::move(*close_error),
                "release_permissions",
                "filesystem release failed and registry lock cleanup also failed");
        }
        return result;
    }

    release_result release_all_permissions()
    {
        registry_state_lock lock;
        const std::filesystem::path state_path = registry_state_path();
        registry_state state = load_registry_state(state_path);

        release_result result;
        release_matching_entries(
            state_path,
            state,
            [](const registry_entry&) {
                return true;
            },
            result);
        if (auto close_error = lock.close())
        {
            merge_final_error(
                result.final_error,
                std::move(*close_error),
                "release_all_permissions",
                "filesystem release-all failed and registry lock cleanup also failed");
        }
        return result;
    }
}
