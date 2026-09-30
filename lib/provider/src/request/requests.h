#pragma once
#include <provider>

namespace provider
{
    Result<nlohmann::json> prepare_request_body(
        Provider selected_provider, const nlohmann::json& body);
}
