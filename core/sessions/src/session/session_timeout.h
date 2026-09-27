#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace sessions::detail
{
    class SessionTimeout final
    {
    public:
        explicit SessionTimeout(std::uint32_t timeout_ms);
        ~SessionTimeout();

        SessionTimeout(const SessionTimeout&) = delete;
        SessionTimeout& operator=(const SessionTimeout&) = delete;
        SessionTimeout(SessionTimeout&&) = delete;
        SessionTimeout& operator=(SessionTimeout&&) = delete;

        void cancel() noexcept;

    private:
        std::mutex mutex_;
        std::condition_variable wake_;
        bool cancelled_ = false;
        std::thread watchdog_;
    };
}
