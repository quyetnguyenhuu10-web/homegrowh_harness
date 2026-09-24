#pragma once

#include <iosfwd>

namespace sandbox::executable
{
    int run_process_broker(std::istream& input, std::ostream& output);
}
