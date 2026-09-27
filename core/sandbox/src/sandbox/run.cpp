#include <sandbox>

#include "run.h"

namespace sandbox
{
    int run(std::string_view body)
    {
        return detail::run_body(body);
    }
}
