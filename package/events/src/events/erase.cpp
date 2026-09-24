#include "repository.h"

#include "../storage/sqlite/statement.h"

namespace events::detail
{
    bool EventRepository::erase(std::int64_t row_position)
    {
        Transaction transaction(database_);
        Statement remove(
            database_,
            "DELETE FROM events WHERE row_position = ?1");
        remove.bind_int64(1, row_position);
        remove.step_done();

        if (sqlite3_changes(database_) == 0)
        {
            transaction.commit();
            return false;
        }

        stage_positions_after(database_, row_position);
        execute_sql(
            database_,
            "UPDATE events SET row_position = -row_position - 2 "
            "WHERE row_position < 0");
        transaction.commit();
        return true;
    }
}