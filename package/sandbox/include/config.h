#pragma once

#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <variant>
#include <vector>

namespace sandbox
{
    enum class network_access : std::uint32_t
    {
        none = 0,
        internet_client = 1,
    };

    namespace detail
    {
        enum class filesystem_access
        {
            read_only,
            read_write,
        };

        struct filesystem_config
        {
            std::filesystem::path path;
            filesystem_access access = filesystem_access::read_only;
        };

        struct read_only_config
        {
            std::filesystem::path path;
        };

        struct read_write_config
        {
            std::filesystem::path path;
        };

        struct network_config
        {
            network_access access = network_access::none;
        };

        struct config_access;
    }

    using config_option = std::variant<
        detail::read_only_config,
        detail::read_write_config,
        detail::network_config>;

    [[nodiscard]] detail::read_only_config read_only(
        std::filesystem::path path);
    [[nodiscard]] detail::read_write_config read_write(
        std::filesystem::path path);
    [[nodiscard]] detail::network_config network(network_access access) noexcept;

    class config final
    {
    public:
        config() = default;
        config(std::initializer_list<config_option> options);

        config& add(config_option option);

    private:
        void add_read_only(const std::filesystem::path& path);
        void add_read_write(const std::filesystem::path& path);
        void add_network(network_access access);
        void add_filesystem(
            const std::filesystem::path& path,
            detail::filesystem_access access);

        std::vector<detail::filesystem_config> filesystem_;
        std::optional<network_access> network_;

        friend struct detail::config_access;
    };
}
