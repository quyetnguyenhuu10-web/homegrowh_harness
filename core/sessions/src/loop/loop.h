#pragma once

#include <session/session.h>

#include <nlohmann/json.hpp>
#include <provider>

namespace sessions
{
    struct LoopResult
    {
        nlohmann::json history;
        provider::RequestUsage usage = provider::UsageState::unavailable;
    };

    LoopResult loop(SessionConfig&& config);
}
