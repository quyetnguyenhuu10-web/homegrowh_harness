#include <event_port>

#include "detail/clock.h"
#include "detail/error.h"
#include "detail/event_access.h"
#include "detail/registry.h"
#include "detail/sequence.h"
#include "detail/validate.h"

#include <utility>

namespace event_port::detail
{
    Result<Registration> PortDispatch::apply(Register&& operation)
    {
        return guard<Registration>("register", [&]() -> Result<Registration>
        {
            auto valid = validate_registration(operation.references);
            if (valid.error)
                return Result<Registration>::failure(context_error(
                    "register", "validation_error", "Registration filter is invalid",
                    {{{"package", operation.package}}}, std::move(*valid.error)));

            auto active_registry = registry();
            if (active_registry.error)
                return Result<Registration>::failure(std::move(*active_registry.error));
            auto state = std::make_shared<RegistrationState>(
                std::move(operation.package), std::move(operation.references));
            auto added = active_registry.value->get().add(state);
            if (added.error)
                return Result<Registration>::failure(context_error(
                    "register", "dependency_error", "Registration could not be added",
                    {{{"package", state->package}}}, std::move(*added.error)));
            return Result<Registration>::success(Registration(std::move(state)));
        });
    }

    Result<EventPtr> PortDispatch::apply(Read&& operation)
    {
        return guard<EventPtr>("read", [&]() -> Result<EventPtr>
        {
            if (operation.registration.state_ == nullptr)
            {
                return Result<EventPtr>::failure(Error{
                    "event_port", "read", "invalid_state", "Registration has been moved from",
                    {{{"state", "moved_from"}}}, {}});
            }
            const auto state = operation.registration.state_;
            std::unique_lock lock(state->mutex);
            state->readable.wait(lock, [&]
            {
                return state->closed || state->pending != nullptr;
            });
            if (state->closed && state->pending == nullptr)
            {
                return Result<EventPtr>::failure(Error{
                    "event_port", "read", "registration_closed", "Registration is closed",
                    {{{"package", state->package}}}, {}});
            }
            EventPtr event = std::move(state->pending);
            lock.unlock();
            state->writable.notify_one();
            return Result<EventPtr>::success(std::move(event));
        });
    }

    Result<EventPtr> PortDispatch::apply(Emit&& operation)
    {
        return guard<EventPtr>("emit", [&]() -> Result<EventPtr>
        {
            auto valid = validate_event(operation.package, operation.type, operation.references);
            if (valid.error)
                return Result<EventPtr>::failure(context_error(
                    "emit", "validation_error", "Event is invalid",
                    {{{"package", operation.package}, {"type", operation.type}}},
                    std::move(*valid.error)));

            auto active_registry = registry();
            if (active_registry.error)
                return Result<EventPtr>::failure(std::move(*active_registry.error));
            auto created = EventAccess::make(
                next_sequence(), now(), std::move(operation.package), operation.level,
                std::move(operation.type), std::move(operation.references), std::move(operation.data));
            if (created.error)
                return Result<EventPtr>::failure(std::move(*created.error));
            auto delivered = active_registry.value->get().publish(*created.value);
            if (delivered.error)
            {
                const Event& event = **created.value;
                return Result<EventPtr>::failure(context_error(
                    "emit", "dependency_error", "Event could not be delivered to all matching registrations",
                    {{{"sequence", event.sequence}, {"package", event.package}, {"type", event.type}}},
                    std::move(*delivered.error)));
            }
            return created;
        });
    }

    Result<void> PortDispatch::apply(Close&& operation)
    {
        return guard<void>("close", [&]() -> Result<void>
        {
            if (operation.registration.state_ == nullptr)
                return Result<void>::failure(Error{
                    "event_port", "close", "invalid_state", "Registration has been moved from",
                    {{{"state", "moved_from"}}}, {}});
            const auto state = operation.registration.state_;
            auto result = close_registration_state(*state, false);
            if (result.error)
                return Result<void>::failure(context_error(
                    "close", "dependency_error", "Registration could not be closed",
                    {{{"package", state->package}}}, std::move(*result.error)));
            return result;
        });
    }
}
