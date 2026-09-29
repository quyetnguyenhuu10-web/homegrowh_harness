#pragma once

#include <sandbox>

namespace sandbox::detail::process::linux
{
    process_results run(const process_request& request);
}
