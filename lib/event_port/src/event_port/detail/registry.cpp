#include "registry.h"
#include "error.h"

#include <algorithm>

namespace event_port::detail
{
    namespace
    {
        bool matches_reference(
            const Reference& filter,
            const References& references)
        {
            return std::any_of(
                references.begin(),
                references.end(),
                [&](const Reference& reference)
                {
                    return
                        reference.type == filter.type &&
                        reference.value == filter.value;
                });
        }

        bool matches(
            const RegistrationState& registration,
            const Event& event)
        {
            if (
                !registration.package.empty() &&
                registration.package != event.package)
            {
                return false;
            }

            return std::all_of(
                registration.references.begin(),
                registration.references.end(),
                [&](const Reference& filter)
                {
                    return matches_reference(filter, event.references);
                });
        }

        Result<void> publish_one(
            const std::shared_ptr<RegistrationState>& registration,
            const EventPtr& event)
        {
            auto result = guard<void>("deliver_event", [&]() -> Result<void>
            {
                if (!matches(*registration, *event))
                    return Result<void>::success();

                std::unique_lock lock(registration->mutex);
                registration->writable.wait(
                    lock,
                    [&]
                    {
                        return registration->closed || registration->pending == nullptr;
                    });
                if (registration->closed)
                    return Result<void>::success();

                registration->pending = event;
                lock.unlock();
                registration->readable.notify_one();
                return Result<void>::success();
            });
            if (result.error)
            {
                nlohmann::json references = nlohmann::json::array();
                for (const auto& reference : registration->references)
                {
                    references.push_back({{"type", reference.type}, {"value", reference.value}});
                }
                result.error->data.emplace_back(nlohmann::json{
                    {"registration_package", registration->package},
                    {"registration_references", std::move(references)},
                    {"event_sequence", event->sequence}
                });
            }
            return result;
        }
    }

    Result<void> Registry::add(const std::shared_ptr<RegistrationState>& state)
    {
        return guard<void>("add_registration", [&]() -> Result<void>
        {
            std::lock_guard lock(mutex_);
            registrations_.emplace_back(state);
            return Result<void>::success();
        });
    }

    Result<void> Registry::publish(const EventPtr& event)
    {
        auto snapshot = guard<std::deque<std::shared_ptr<RegistrationState>>>(
            "snapshot_registrations", [&]() -> Result<std::deque<std::shared_ptr<RegistrationState>>>
        {
            std::deque<std::shared_ptr<RegistrationState>> registrations;
            std::lock_guard lock(mutex_);

            auto current = registrations_.begin();
            while (current != registrations_.end())
            {
                if (std::shared_ptr<RegistrationState> state = current->lock())
                {
                    registrations.emplace_back(std::move(state));
                    ++current;
                    continue;
                }

                current = registrations_.erase(current);
            }
            return Result<std::deque<std::shared_ptr<RegistrationState>>>::success(std::move(registrations));
        });
        if (snapshot.error)
            return Result<void>::failure(std::move(*snapshot.error));

        return guard<void>("publish", [&]() -> Result<void>
        {
            std::vector<Error> errors;
            for (const auto& registration : *snapshot.value)
            {
                auto delivered = publish_one(registration, event);
                if (delivered.error)
                    errors.emplace_back(std::move(*delivered.error));
            }
            if (errors.empty())
                return Result<void>::success();
            if (errors.size() == 1)
                return Result<void>::failure(std::move(errors.front()));
            return Result<void>::failure(Error{
                "event_port", "publish", "dependency_error",
                "Event delivery failed for multiple registrations",
                {{{"event_sequence", event->sequence}, {"failed_registrations", errors.size()}}},
                std::move(errors)});
        });
    }

    Result<std::reference_wrapper<Registry>> registry()
    {
        return guard<std::reference_wrapper<Registry>>("registry", []() -> Result<std::reference_wrapper<Registry>>
        {
            static Registry instance;
            return Result<std::reference_wrapper<Registry>>::success(std::ref(instance));
        });
    }
}
