#include "registry.h"

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

        void publish_one(
            const std::shared_ptr<RegistrationState>& registration,
            const EventPtr& event)
        {
            if (!matches(*registration, *event))
            {
                return;
            }

            std::unique_lock lock(registration->mutex);
            registration->writable.wait(
                lock,
                [&]
                {
                    return
                        registration->closed ||
                        registration->pending == nullptr;
                });

            if (registration->closed)
            {
                return;
            }

            registration->pending = event;
            lock.unlock();
            registration->readable.notify_one();
        }
    }

    void Registry::add(const std::shared_ptr<RegistrationState>& state)
    {
        std::lock_guard lock(mutex_);
        registrations_.emplace_back(state);
    }

    void Registry::publish(const EventPtr& event)
    {
        std::deque<std::shared_ptr<RegistrationState>> registrations;

        {
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
        }

        for (const std::shared_ptr<RegistrationState>& registration : registrations)
        {
            publish_one(registration, event);
        }
    }

    Registry& registry()
    {
        static Registry instance;
        return instance;
    }
}
