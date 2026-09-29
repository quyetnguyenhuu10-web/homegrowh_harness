#include <event_port>

#include "detail/clock.h"
#include "detail/event_access.h"
#include "detail/registry.h"
#include "detail/sequence.h"
#include "detail/validate.h"

#include <stdexcept>
#include <utility>

namespace event_port::detail
{
    Registration PortDispatch::apply(Register&& operation)
    {
        validate_registration(operation.references);

        auto state = std::make_shared<RegistrationState>(
            std::move(operation.package),
            std::move(operation.references));

        registry().add(state);
        return Registration(std::move(state));
    }

    EventPtr PortDispatch::apply(Read&& operation)
    {
        if (operation.registration.state_ == nullptr)
        {
            throw std::logic_error(
                "event_port: registration has been moved from");
        }

        const std::shared_ptr<RegistrationState> state =
            operation.registration.state_;

        std::unique_lock lock(state->mutex);
        state->readable.wait(
            lock,
            [&]
            {
                return state->closed || state->pending != nullptr;
            });

        if (state->closed && state->pending == nullptr)
        {
            throw std::logic_error("event_port: registration is closed");
        }

        EventPtr event = std::move(state->pending);
        lock.unlock();
        state->writable.notify_one();
        return event;
    }

    EventPtr PortDispatch::apply(Emit&& operation)
    {
        validate_event(
            operation.package,
            operation.type,
            operation.references);

        EventPtr event = EventAccess::make(
            next_sequence(),
            now(),
            std::move(operation.package),
            operation.level,
            std::move(operation.type),
            std::move(operation.references),
            std::move(operation.data));

        registry().publish(event);
        return event;
    }

    void PortDispatch::apply(Close&& operation)
    {
        if (operation.registration.state_ == nullptr)
        {
            throw std::logic_error(
                "event_port: registration has been moved from");
        }

        const std::shared_ptr<RegistrationState> state =
            operation.registration.state_;

        close_registration_state(*state, false);
    }
}
