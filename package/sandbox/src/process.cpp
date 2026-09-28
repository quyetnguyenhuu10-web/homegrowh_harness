#include <sandbox_process.h>

#if defined(_WIN32)

#include "window/process.h"

#elif defined(__linux__)

#include "linux/process.h"

#else

#error "Unsupported operating system"

#endif

namespace sandbox
{
    void process(process_request& request)
    {
#if defined(_WIN32)
        request.results = detail::process::windows::run(request);
#elif defined(__linux__)
        request.results = detail::process::linux::run(request);
#endif
    }
}
