#include <sandbox>

#include <algorithm>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace sandbox
{
    detail::read_only_config read_only(std::filesystem::path path)
    {
        return {std::move(path)};
    }

    detail::read_write_config read_write(std::filesystem::path path)
    {
        return {std::move(path)};
    }

    detail::network_config network(network_access access) noexcept
    {
        return {access};
    }

    config no_filesystem() noexcept
    {
        return {};
    }

    config::config(std::initializer_list<config_option> options)
    {
        for (const config_option& option : options)
            add(option);
    }

    config& config::add(config_option option)
    {
        std::visit(
            [this](const auto& value)
            {
                using value_type = std::decay_t<decltype(value)>;
                if constexpr (
                    std::is_same_v<value_type, detail::read_only_config>)
                {
                    add_read_only(value.path);
                }
                else if constexpr (
                    std::is_same_v<value_type, detail::read_write_config>)
                {
                    add_read_write(value.path);
                }
                else if constexpr (std::is_same_v<value_type, detail::network_config>)
                    add_network(value.access);
            },
            option);
        return *this;
    }

    void config::add_read_only(const std::filesystem::path& path)
    {
        add_filesystem(path, detail::filesystem_access::read_only);
    }

    void config::add_read_write(const std::filesystem::path& path)
    {
        add_filesystem(path, detail::filesystem_access::read_write);
    }

    void config::add_network(network_access access)
    {
        if (network_.has_value() && *network_ != access)
            throw std::invalid_argument("sandbox config contains conflicting network policies");
        network_ = access;
    }

    void config::add_filesystem(
        const std::filesystem::path& path,
        detail::filesystem_access access)
    {
        if (path.empty())
            throw std::invalid_argument("sandbox filesystem path must not be empty");

        const auto found = std::find_if(
            filesystem_.begin(),
            filesystem_.end(),
            [&](const detail::filesystem_config& item)
            {
                return item.path == path;
            });

        if (found == filesystem_.end())
        {
            filesystem_.push_back({path, access});
            return;
        }

        if (found->access != access)
        {
            throw std::invalid_argument(
                "sandbox config cannot activate read and write for the same path");
        }
    }
}
