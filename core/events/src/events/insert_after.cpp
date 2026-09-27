#include "repository.h"

#include "../storage/sqlite/statement.h"

#include <limits>
#include <stdexcept>

namespace events::detail
{
    std::int64_t EventRepository::insert_after(
        std::int64_t row_position,
        const EventInput& event)
    {
        if (row_position == (std::numeric_limits<std::int64_t>::max)())
        {
            throw std::overflow_error("events: row_position overflow");
        }

        Transaction transaction(database_);

        if (!position_exists(database_, row_position))
        {
            throw std::out_of_range(
                "events: row_position does not exist");
        }

        stage_positions_after(database_, row_position);
        execute_sql(
            database_,
            "UPDATE events SET row_position = -row_position "
            "WHERE row_position < 0");

        const std::int64_t inserted_position = row_position + 1;
        insert_event(database_, inserted_position, event);
        transaction.commit();
        return inserted_position;
    }
}