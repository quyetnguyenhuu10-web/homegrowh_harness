#pragma once

#include <sandbox_process.h>
#include <registry.h>

#include <system_error>

namespace sandbox::detail::process::linux
{
    std::error_code apply_landlock(
        const process_request& request,
        const registry_result& registry) noexcept;
}
