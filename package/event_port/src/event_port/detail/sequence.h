#pragma once

#include <cstdint>

namespace event_port::detail
{
    std::uint64_t next_sequence() noexcept;
}
