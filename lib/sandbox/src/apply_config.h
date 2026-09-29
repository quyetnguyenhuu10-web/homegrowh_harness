#pragma once

#include <sandbox>
#include <registry.h>

#include <cstddef>
#include <vector>

namespace sandbox::detail
{
    struct applied_config
    {
        registry_result filesystem;
        config_results results;
        network_access network = network_access::none;
        std::size_t filesystem_count = 0;
    };

    struct config_access
    {
        static const std::vector<filesystem_config>& filesystem(
            const sandbox::config& config) noexcept;

        static network_access network(
            const sandbox::config& config) noexcept;
    };

    applied_config apply_config(
        const sandbox::config& config,
        bool refresh);
}
