#pragma once

#include <sandbox_process.h>

namespace sandbox::detail::process::windows
{
    process_results run(const process_request& request);
}
