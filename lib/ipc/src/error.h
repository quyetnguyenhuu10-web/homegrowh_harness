#pragma once

#include <ipc>

#include <exception>
#include <string_view>
#include <system_error>

namespace ipc::detail
{
    Error make_error(
        std::string_view operation,
        std::string_view type,
        std::string_view message,
        nlohmann::json&& context = nullptr);

    Error make_system_error(
        std::string_view operation,
        const std::error_code& code,
        std::string_view api,
        nlohmann::json&& context);

    Error make_exception_error(
        std::string_view operation,
        const std::exception& exception,
        nlohmann::json&& context = nullptr);

    void add_cleanup_error(
        std::optional<Error>& error,
        Error&& cleanup_error);

    std::optional<Error> validate_name(
        const std::string& name,
        std::string_view operation);
}
