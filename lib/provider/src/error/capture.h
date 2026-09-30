#pragma once

#include "error.h"

#include <exception>
#include <string_view>

namespace provider::error_detail
{
    Error make_error(
        std::string_view operation,
        std::string_view type,
        std::string_view message,
        nlohmann::json::array_t&& data = {});

    Error capture_exception(
        const std::exception_ptr& exception,
        std::string_view operation,
        nlohmann::json::array_t&& data = {});

    Error dependency_error(
        std::string_view operation,
        std::string_view message,
        Error&& cause,
        nlohmann::json::array_t&& data = {});
}
