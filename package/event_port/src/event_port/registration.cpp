#include <event_port>

#include "detail/registration_state.h"

#include <stdexcept>
#include <utility>

namespace event_port
{
    namespace
    {
        void close_registration(
            std::shared_ptr<detail::RegistrationState>& state) noexcept
        {
            if (state == nullptr)
            {
                return;
            }

            detail::close_registration_state(*state, true);
            state.reset();
        }
    }

    Registration::Registration(
        std::shared_ptr<detail::RegistrationState>&& state) noexcept
        : state_(std::move(state))
    {
    }

    Registration::~Registration()
    {
        close_registration(state_);
    }

    Registration::Registration(Registration&& other) noexcept
        : state_(std::move(other.state_))
    {
    }

    Registration& Registration::operator=(Registration&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        close_registration(state_);
        state_ = std::move(other.state_);
        return *this;
    }
}
