#include "clock.h"

namespace event_port::detail
{
    std::chrono::system_clock::time_point now() noexcept
    {
        return std::chrono::system_clock::now();
    }
}
