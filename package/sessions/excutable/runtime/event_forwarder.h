#pragma once

#include <exception>
#include <mutex>
#include <optional>
#include <thread>

#include <event_port>
#include <ipc>

namespace sessions_runtime
{
    class EventForwarder final
    {
    public:
        explicit EventForwarder(ipc::connection& connection);

        EventForwarder(const EventForwarder&) = delete;
        EventForwarder& operator=(const EventForwarder&) = delete;

        ~EventForwarder();

        void start();
        void stop() noexcept;
        void rethrow_if_failed() const;

    private:
        void run() noexcept;
        void set_error(std::exception_ptr error) noexcept;

        ipc::connection& connection_;
        std::optional<event_port::Registration> registration_;
        std::thread thread_;

        mutable std::mutex error_mutex_;
        std::exception_ptr error_;
        bool started_ = false;
    };
}
