#pragma once

#include "../process.h"

namespace tool_runtime::detail::process_platform::windows
{
    process_result run(const process_request& request);
}
