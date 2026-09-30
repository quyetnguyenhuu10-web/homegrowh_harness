#pragma once

#include <exception>
#include <string_view>

#include <nlohmann/json.hpp>
#include <provider>
#include <session>

namespace sessions_runtime
{
    const char* provider_name(provider::Provider value) noexcept;
    const char* session_state_name(sessions::SessionState state) noexcept;

    nlohmann::json usage_json(
        const provider::RequestUsage& usage);
    nlohmann::json error_json(
        std::exception_ptr error,
        std::string_view operation = "error_json");
}
