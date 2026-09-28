#pragma once

#include <exception>

#include <nlohmann/json.hpp>
#include <provider>
#include <sessions>

namespace sessions_runtime
{
    const char* provider_name(provider::Provider value) noexcept;
    const char* session_state_name(sessions::SessionState state) noexcept;

    nlohmann::json usage_json(
        const provider::RequestUsage& usage);
    nlohmann::json error_json(std::exception_ptr error);
}
