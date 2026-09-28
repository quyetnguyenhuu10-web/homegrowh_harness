#pragma once

#include <sandbox_process.h>

namespace sandbox::detail::process::linux
{
    process_results run(const process_request& request);
}
