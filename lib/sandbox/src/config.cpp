#include <sandbox>

#include "error_schema.h"

#include <algorithm>
#include <type_traits>
#include <utility>

namespace sandbox
{
    namespace
    {
        const char* filesystem_access_name(detail::filesystem_access access) noexcept
        {
            return access == detail::filesystem_access::read_only
                ? "read_only"
                : "read_write";
        }
    }

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
        {
            auto result = add(option);
            if (result.error)
                break;
        }
    }

    const std::optional<Error>& config::error() const noexcept
    {
        return error_;
    }

    Result<void> config::add(const config_option& option)
    {
        if (error_)
            return Result<void>::failure(Error(*error_));
        try
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
            return Result<void>::success();
        }
        catch (...)
        {
            error_ = detail::capture_exception("config.add", std::current_exception());
            return Result<void>::failure(Error(*error_));
        }
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
        if (access != network_access::none && access != network_access::internet_client)
        {
            detail::throw_error(detail::make_error(
                "config.add_network",
                "invalid_argument",
                "sandbox network policy is invalid",
                {{"requested_access", static_cast<std::uint32_t>(access)}}));
        }
        if (network_.has_value() && *network_ != access)
        {
            detail::throw_error(detail::make_error(
                "config.add_network",
                "invalid_argument",
                "sandbox config contains conflicting network policies",
                {{{"existing_access", static_cast<std::uint32_t>(*network_)},
                  {"requested_access", static_cast<std::uint32_t>(access)}}}));
        }
        network_ = access;
    }

    void config::add_filesystem(
        const std::filesystem::path& path,
        detail::filesystem_access access)
    {
        if (path.empty())
        {
            detail::throw_error(detail::make_error(
                "config.add_filesystem",
                "invalid_argument",
                "sandbox filesystem path must not be empty",
                {{{"access", filesystem_access_name(access)}}}));
        }

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
            detail::throw_error(detail::make_error(
                "config.add_filesystem",
                "invalid_argument",
                "sandbox config cannot activate read and write for the same path",
                {{{"path", detail::error_path_text(path)},
                  {"existing_access", filesystem_access_name(found->access)},
                  {"requested_access", filesystem_access_name(access)}}}));
        }
    }
}
