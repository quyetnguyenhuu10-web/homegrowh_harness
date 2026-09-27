#include "registration_state.h"

#include <utility>

namespace event_port::detail
{
    RegistrationState::RegistrationState(
        std::string&& package_value,
        References&& references_value) noexcept
        : package(std::move(package_value)),
          references(std::move(references_value))
    {
    }

    void close_registration_state(
        RegistrationState& state,
        bool discard_pending) noexcept
    {
        {
            std::lock_guard lock(state.mutex);
            state.closed = true;
            if (discard_pending)
            {
                state.pending.reset();
            }
        }

        state.readable.notify_all();
        state.writable.notify_all();
    }
}
