#include "session_timeout.h"

#include <chrono>
#include <cstdlib>

namespace sessions::detail
{
    SessionTimeout::SessionTimeout(std::uint32_t timeout_ms)
        : watchdog_(
            [this, timeout_ms]
            {
                std::unique_lock lock(mutex_);
                const bool cancelled = wake_.wait_for(
                    lock,
                    std::chrono::milliseconds(timeout_ms),
                    [this]
                    {
                        return cancelled_;
                    });

                if (cancelled)
                    return;

                std::abort();
            })
    {
    }

    SessionTimeout::~SessionTimeout()
    {
        cancel();
        if (watchdog_.joinable())
            watchdog_.join();
    }

    void SessionTimeout::cancel() noexcept
    {
        {
            std::lock_guard lock(mutex_);
            if (cancelled_)
                return;
            cancelled_ = true;
        }
        wake_.notify_all();
    }
}
