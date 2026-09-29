#pragma once

#include <sandbox>

namespace sandbox::detail::process::windows
{
    process_results run(const process_request& request);
}
