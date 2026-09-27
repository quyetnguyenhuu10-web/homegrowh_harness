#pragma once

#include "../api/process.h"

namespace sandbox::detail::process::windows
{
    process_result run(const process_request& request);
}
