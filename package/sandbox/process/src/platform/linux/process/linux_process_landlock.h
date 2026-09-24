#pragma once

#include <sandbox/process.h>

#include <system_error>

namespace sandbox::detail::process::linux
{
    std::error_code apply_landlock(
        const process_request& request,
        const registry_result& registry) noexcept;
}
