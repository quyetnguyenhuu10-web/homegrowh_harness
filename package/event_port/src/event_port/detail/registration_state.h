#pragma once

#include <event_port>

#include <condition_variable>
#include <mutex>

namespace event_port::detail
{
    struct RegistrationState
    {
        RegistrationState(
            std::string&& package_value,
            References&& references_value) noexcept;

        std::string package;
        References references;

        std::mutex mutex;
        std::condition_variable readable;
        std::condition_variable writable;
        EventPtr pending;
        bool closed = false;
    };

    void close_registration_state(
        RegistrationState& state,
        bool discard_pending) noexcept;
}
