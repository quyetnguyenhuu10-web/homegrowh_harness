#include "apply_config.h"

#include <registry.h>

#include <utility>

namespace sandbox::detail
{
    const std::vector<filesystem_config>& config_access::filesystem(
        const sandbox::config& config) noexcept
    {
        return config.filesystem_;
    }

    network_access config_access::network(
        const sandbox::config& config) noexcept
    {
        return config.network_.value_or(network_access::none);
    }

    applied_config apply_config(
        const sandbox::config& config,
        bool refresh)
    {
        const auto& filesystem = config_access::filesystem(config);
        std::vector<registry_request> requests;
        requests.reserve(filesystem.size());
        for (const filesystem_config& item : filesystem)
        {
            requests.push_back({
                item.path,
                item.access == filesystem_access::read_only
                    ? permission::read_only
                    : permission::read_write,
            });
        }

        applied_config applied;
        applied.filesystem_count = requests.size();
        applied.filesystem = registry(requests, refresh);
        applied.results.final_error = applied.filesystem.final_error;
        applied.results.path_errors.reserve(applied.filesystem.path_errors.size());
        for (registry_path_error& item : applied.filesystem.path_errors)
        {
            applied.results.path_errors.push_back({
                std::move(item.path),
                item.error,
            });
        }
        applied.network = config_access::network(config);
        return applied;
    }
}
