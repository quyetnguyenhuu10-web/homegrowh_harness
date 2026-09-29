#pragma once

#include "registration_state.h"

#include <deque>
#include <memory>
#include <mutex>

namespace event_port::detail
{
    class Registry
    {
    public:
        void add(const std::shared_ptr<RegistrationState>& state);
        void publish(const EventPtr& event);

    private:
        std::mutex mutex_;
        std::deque<std::weak_ptr<RegistrationState>> registrations_;
    };

    Registry& registry();
}
