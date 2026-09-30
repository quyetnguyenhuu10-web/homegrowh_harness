#pragma once

#include "../framing.h"

namespace ipc::detail
{
    exact_read_result read_exact(
        int descriptor, void* output, std::size_t size,
        const std::string& path, std::string_view phase);

    std::optional<Error> write_exact(
        int descriptor, const void* input, std::size_t size,
        const std::string& path, std::string_view phase);
}
