#include <events>

#include "store_impl.h"

#include <stdexcept>

namespace events
{
    Store::Store(const std::filesystem::path& database_path)
        : impl_(std::make_unique<Impl>(database_path))
    {
    }

    Store::~Store() = default;

    Store::Store(Store&&) noexcept = default;

    Store& Store::operator=(Store&&) noexcept = default;

    std::int64_t append(Store& store, const EventInput& event)
    {
        if (store.impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(store.impl_->mutex);
        return store.impl_->repository.append(event);
    }

    bool erase(Store& store, std::int64_t row_position)
    {
        if (store.impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(store.impl_->mutex);
        return store.impl_->repository.erase(row_position);
    }

    std::vector<Event> query(const Store& store)
    {
        if (store.impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(store.impl_->mutex);
        return store.impl_->repository.query();
    }

    std::vector<Event> query(
        const Store& store,
        const std::string& session_id)
    {
        if (store.impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(store.impl_->mutex);
        return store.impl_->repository.query(session_id);
    }

    std::int64_t insert_after(
        Store& store,
        std::int64_t row_position,
        const EventInput& event)
    {
        if (store.impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(store.impl_->mutex);
        return store.impl_->repository.insert_after(row_position, event);
    }
}
