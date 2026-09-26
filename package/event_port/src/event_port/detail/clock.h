#pragma once

#include <chrono>

namespace event_port::detail
{
    std::chrono::system_clock::time_point now() noexcept;
}
