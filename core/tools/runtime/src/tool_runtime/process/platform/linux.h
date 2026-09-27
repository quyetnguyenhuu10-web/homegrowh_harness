#pragma once

#include "../process.h"

namespace tool_runtime::detail::process_platform::linux
{
    process_result run(const process_request& request);
}
