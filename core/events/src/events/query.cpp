#include "repository.h"

#include "../storage/sqlite/statement.h"

namespace events::detail
{
    std::vector<Event> EventRepository::query() const
    {
        Statement statement(
            database_,
            "SELECT row_position, events, create_at, Session_ID, provider, model "
            "FROM events ORDER BY row_position");
        std::vector<Event> result;

        while (statement.step_row())
        {
            result.push_back(read_event(statement));
        }

        return result;
    }

    std::vector<Event> EventRepository::query(
        const std::string& session_id) const
    {
        Statement statement(
            database_,
            "SELECT row_position, events, create_at, Session_ID, provider, model "
            "FROM events WHERE Session_ID = ?1 ORDER BY row_position");
        statement.bind_text(1, session_id);
        std::vector<Event> result;

        while (statement.step_row())
        {
            result.push_back(read_event(statement));
        }

        return result;
    }
}
