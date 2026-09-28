#pragma once

#include <sessions>

#include <nlohmann/json.hpp>

namespace sessions_runtime
{
    sessions::SessionConfig parse_session_config(
        nlohmann::json&& input);
}
