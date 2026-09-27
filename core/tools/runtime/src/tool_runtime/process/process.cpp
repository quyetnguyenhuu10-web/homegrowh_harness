#include "process.h"

#if defined(_WIN32)
#include "platform/windows.h"
#elif defined(__linux__)
#include "platform/linux.h"
#else
#error "Unsupported operating system"
#endif

namespace tool_runtime::detail
{
    process_result run_process(const process_request& request)
    {
#if defined(_WIN32)
        return process_platform::windows::run(request);
#else
        return process_platform::linux::run(request);
#endif
    }
}
