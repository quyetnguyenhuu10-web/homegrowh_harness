#pragma once

#include <request/requests.h>

#include <cstdint>

namespace provider
{
    std::uint64_t context_usage(const RequestUsage& usage);
}
