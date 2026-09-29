#pragma once

#include <registry.h>

#include <system_error>

namespace sandbox::detail::process::linux
{
    std::error_code apply_landlock(const registry_result& registry) noexcept;
}
