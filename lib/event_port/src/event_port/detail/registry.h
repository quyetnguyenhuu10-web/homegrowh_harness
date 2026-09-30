#pragma once

#include "registration_state.h"

#include <deque>
#include <functional>
#include <memory>
#include <mutex>

namespace event_port::detail
{
    class Registry
    {
    public:
        Result<void> add(const std::shared_ptr<RegistrationState>& state);
        Result<void> publish(const EventPtr& event);

    private:
        std::mutex mutex_;
        std::deque<std::weak_ptr<RegistrationState>> registrations_;
    };

    Result<std::reference_wrapper<Registry>> registry();
}
