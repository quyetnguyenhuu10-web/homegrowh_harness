#include "registration_state.h"
#include "error.h"

#include <utility>

namespace event_port::detail
{
    RegistrationState::RegistrationState(
        std::string&& package_value,
        References&& references_value)
        : package(std::move(package_value)),
          references(std::move(references_value))
    {
    }

    Result<void> close_registration_state(
        RegistrationState& state,
        bool discard_pending)
    {
        auto result = guard<void>("close_registration", [&]() -> Result<void>
        {
            std::lock_guard lock(state.mutex);
            state.closed = true;
            if (discard_pending)
            {
                state.pending.reset();
            }
            return Result<void>::success();
        });

        state.readable.notify_all();
        state.writable.notify_all();
        return result;
    }
}
