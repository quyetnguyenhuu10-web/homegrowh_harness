#pragma once

#include "raii.h"

namespace sandbox::detail::process::windows
{
    unique_handle create_process_job();

    void assign_process_to_job(HANDLE job, HANDLE process);
}
