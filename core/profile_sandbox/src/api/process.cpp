#include "process.h"

#if defined(_WIN32)

#include "../windows/process.h"

#elif defined(__linux__)

#include "../linux/process.h"

#else

#error "Unsupported operating system"

#endif

namespace sandbox
{
    process_result process(const process_request& request)
    {
#if defined(_WIN32)
        return detail::process::windows::run(request);
#elif defined(__linux__)
        return detail::process::linux::run(request);
#endif
    }
}
