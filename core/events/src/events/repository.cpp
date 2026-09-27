#include "repository.h"

#include "../storage/sqlite/statement.h"

#include <stdexcept>

namespace events::detail
{
    EventRepository::EventRepository(sqlite3* database)
        : database_(database)
    {
        if (database_ == nullptr)
        {
            throw std::invalid_argument("events: SQLite database is null");
        }

        execute_sql(
            database_,
            "CREATE TABLE IF NOT EXISTS events ("
            "row_position INTEGER PRIMARY KEY,"
            "events TEXT NOT NULL,"
            "create_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "Session_ID TEXT NOT NULL,"
            "provider TEXT NOT NULL,"
            "model TEXT NOT NULL"
            ");"
            "CREATE INDEX IF NOT EXISTS events_session_position "
            "ON events(Session_ID, row_position);");
    }

    std::int64_t EventRepository::maximum_position(sqlite3* database)
    {
        Statement statement(
            database,
            "SELECT COALESCE(MAX(row_position), -1) FROM events");

        if (!statement.step_row())
        {
            throw std::runtime_error(
                "events: failed to read the last row position");
        }

        return statement.column_int64(0);
    }

    bool EventRepository::position_exists(
        sqlite3* database,
        std::int64_t position)
    {
        Statement statement(
            database,
            "SELECT 1 FROM events WHERE row_position = ?1");
        statement.bind_int64(1, position);
        return statement.step_row();
    }

    void EventRepository::stage_positions_after(
        sqlite3* database,
        std::int64_t position)
    {
        Statement stage(
            database,
            "UPDATE events "
            "SET row_position = -row_position - 1 "
            "WHERE row_position > ?1");
        stage.bind_int64(1, position);
        stage.step_done();
    }

    void EventRepository::insert_event(
        sqlite3* database,
        std::int64_t position,
        const EventInput& event)
    {
        Statement statement(
            database,
            "INSERT INTO events "
            "(row_position, events, create_at, Session_ID, provider, model) "
            "VALUES (?1, ?2, COALESCE(?3, "
            "strftime('%Y-%m-%dT%H:%M:%fZ', 'now')), ?4, ?5, ?6)");
        statement.bind_int64(1, position);
        statement.bind_text(2, event.events);
        statement.bind_optional_text(3, event.create_at);
        statement.bind_text(4, event.session_id);
        statement.bind_text(5, event.provider);
        statement.bind_text(6, event.model);
        statement.step_done();
    }

    Event EventRepository::read_event(const Statement& statement)
    {
        Event result;
        result.row_position = statement.column_int64(0);
        result.events = statement.column_text(1);
        result.create_at = statement.column_text(2);
        result.session_id = statement.column_text(3);
        result.provider = statement.column_text(4);
        result.model = statement.column_text(5);
        return result;
    }
}