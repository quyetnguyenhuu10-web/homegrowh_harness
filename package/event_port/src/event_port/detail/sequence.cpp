#include "sequence.h"

#include <atomic>

namespace event_port::detail
{
    namespace
    {
        std::atomic<std::uint64_t> sequence{0};
    }

    std::uint64_t next_sequence() noexcept
    {
        return sequence.fetch_add(1, std::memory_order_relaxed);
    }
}
