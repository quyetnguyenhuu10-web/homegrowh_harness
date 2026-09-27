#pragma once

#include <optional>
#include <string_view>

#include <nlohmann/json_fwd.hpp>
#include <provider>

namespace sessions::detail
{
    std::optional<std::string_view> reasoning_delta(
        provider::Provider provider,
        const nlohmann::json& delta);
}
