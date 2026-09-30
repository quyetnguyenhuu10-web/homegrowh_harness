#pragma once

#include "resource.h"
#include "../framing.h"

namespace ipc::detail
{
    Error win32_error(
        std::string_view operation, DWORD code,
        std::string_view api, nlohmann::json&& context);

    exact_read_result read_exact(
        HANDLE handle, void* output, std::size_t size, bool overlapped,
        const std::string& path, std::string_view phase);

    std::optional<Error> write_exact(
        HANDLE handle, const void* input, std::size_t size, bool overlapped,
        const std::string& path, std::string_view phase);
}
