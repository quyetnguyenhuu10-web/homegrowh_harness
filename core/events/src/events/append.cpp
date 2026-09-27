#include "repository.h"

#include "../storage/sqlite/statement.h"

#include <limits>
#include <stdexcept>

namespace events::detail
{
    std::int64_t EventRepository::append(const EventInput& event)
    {
        Transaction transaction(database_);
        const std::int64_t last_position = maximum_position(database_);

        if (last_position == (std::numeric_limits<std::int64_t>::max)())
        {
            throw std::overflow_error("events: row_position overflow");
        }

        const std::int64_t position = last_position + 1;
        insert_event(database_, position, event);
        transaction.commit();
        return position;
    }
}