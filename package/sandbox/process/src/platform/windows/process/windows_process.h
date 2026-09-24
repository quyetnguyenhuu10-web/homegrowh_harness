#pragma once

#include <sandbox/process.h>

namespace sandbox::detail::process::windows
{
    process_result run(const process_request& request);
}
