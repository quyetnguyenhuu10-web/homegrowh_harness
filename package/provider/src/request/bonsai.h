#pragma once

#include <cstdint>

namespace provider
{
    struct BonsaiUsage
    {
        std::uint64_t cache_n = 0;
        std::uint64_t prompt_n = 0;
        double prompt_ms = 0.0;
        double prompt_per_token_ms = 0.0;
        double prompt_per_second = 0.0;
        std::uint64_t predicted_n = 0;
        double predicted_ms = 0.0;
        double predicted_per_token_ms = 0.0;
        double predicted_per_second = 0.0;
    };
}
