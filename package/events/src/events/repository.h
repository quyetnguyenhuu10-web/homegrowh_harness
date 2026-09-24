#pragma once

#include <events>

#include <sqlite3.h>

#include <cstdint>
#include <vector>

namespace events::detail
{
    class Statement;

    class EventRepository
    {
    public:
        explicit EventRepository(sqlite3* database);

        std::int64_t append(const EventInput& event);
        bool erase(std::int64_t row_position);
        std::vector<Event> query() const;
        std::vector<Event> query(const std::string& session_id) const;
        std::int64_t insert_after(
            std::int64_t row_position,
            const EventInput& event);

    private:
        static std::int64_t maximum_position(sqlite3* database);
        static bool position_exists(sqlite3* database, std::int64_t position);
        static void stage_positions_after(
            sqlite3* database,
            std::int64_t position);
        static void insert_event(
            sqlite3* database,
            std::int64_t position,
            const EventInput& event);
        static Event read_event(const Statement& statement);

        sqlite3* database_ = nullptr;
    };
}
