#pragma once

#include <sandbox/process.h>

namespace sandbox::detail::process::linux
{
    process_result run(const process_request& request);
}
