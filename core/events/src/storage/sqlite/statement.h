#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <string>

namespace events::detail
{
    [[noreturn]] void throw_sqlite_error(
        sqlite3* database,
        const char* operation,
        int result);

    void check_sqlite(
        sqlite3* database,
        const char* operation,
        int result);

    void execute_sql(sqlite3* database, const char* sql);

    class Statement
    {
    public:
        Statement(sqlite3* database, const char* sql);
        ~Statement();

        Statement(const Statement&) = delete;
        Statement& operator=(const Statement&) = delete;

        void bind_int64(int index, std::int64_t value);
        void bind_text(int index, const std::string& value);
        void bind_optional_text(
            int index,
            const std::optional<std::string>& value);

        bool step_row();
        void step_done();

        std::int64_t column_int64(int column) const;
        std::string column_text(int column) const;

    private:
        sqlite3* database_ = nullptr;
        sqlite3_stmt* statement_ = nullptr;
    };

    class Transaction
    {
    public:
        explicit Transaction(sqlite3* database);
        ~Transaction();

        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

        void commit();

    private:
        sqlite3* database_ = nullptr;
        bool active_ = true;
    };
}
