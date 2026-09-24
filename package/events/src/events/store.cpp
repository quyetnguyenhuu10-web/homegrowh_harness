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

    std::int64_t Store::append(const EventInput& event)
    {
        if (impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(impl_->mutex);
        return impl_->repository.append(event);
    }

    bool Store::erase(std::int64_t row_position)
    {
        if (impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(impl_->mutex);
        return impl_->repository.erase(row_position);
    }

    std::vector<Event> Store::query() const
    {
        if (impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(impl_->mutex);
        return impl_->repository.query();
    }

    std::vector<Event> Store::query(const std::string& session_id) const
    {
        if (impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(impl_->mutex);
        return impl_->repository.query(session_id);
    }

    std::int64_t Store::insert_after(
        std::int64_t row_position,
        const EventInput& event)
    {
        if (impl_ == nullptr)
        {
            throw std::logic_error("events: store has been moved from");
        }

        std::lock_guard lock(impl_->mutex);
        return impl_->repository.insert_after(row_position, event);
    }
}
