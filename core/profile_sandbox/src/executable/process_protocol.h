#pragma once

#include "../api/process.h"

#include <iosfwd>
#include <string>

namespace sandbox::executable::process_protocol
{
    bool read_request(
        std::istream& input,
        process_request& request,
        std::string& error);

    bool write_result(
        std::ostream& output,
        const process_result& result);
}
