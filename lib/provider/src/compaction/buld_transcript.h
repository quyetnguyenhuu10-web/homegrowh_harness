#pragma once

#include <provider>

namespace provider
{
    Result<nlohmann::json> build_transcript(const nlohmann::json& message);
}
