#include "statement.h"

#include <limits>
#include <stdexcept>
#include <string>

namespace events::detail
{
    [[noreturn]] void throw_sqlite_error(
        sqlite3* database,
        const char* operation,
        int result)
    {
        const char* message = database == nullptr
            ? "unknown SQLite error"
            : sqlite3_errmsg(database);

        throw std::runtime_error(
            std::string("events: SQLite ") + operation + " failed (" +
            std::to_string(result) + "): " + message);
    }

    void check_sqlite(
        sqlite3* database,
        const char* operation,
        int result)
    {
        if (result != SQLITE_OK)
        {
            throw_sqlite_error(database, operation, result);
        }
    }

    void execute_sql(sqlite3* database, const char* sql)
    {
        char* error_message = nullptr;
        const int result = sqlite3_exec(
            database,
            sql,
            nullptr,
            nullptr,
            &error_message);

        if (result == SQLITE_OK)
        {
            return;
        }

        const std::string detail = error_message == nullptr
            ? sqlite3_errmsg(database)
            : error_message;
        sqlite3_free(error_message);

        throw std::runtime_error(
            std::string("events: SQLite statement failed (") +
            std::to_string(result) + "): " + detail);
    }

    Statement::Statement(sqlite3* database, const char* sql)
        : database_(database)
    {
        const int result = sqlite3_prepare_v2(
            database_,
            sql,
            -1,
            &statement_,
            nullptr);

        if (result != SQLITE_OK)
        {
            sqlite3_finalize(statement_);
            statement_ = nullptr;
            throw_sqlite_error(database_, "prepare", result);
        }
    }

    Statement::~Statement()
    {
        sqlite3_finalize(statement_);
    }

    void Statement::bind_int64(int index, std::int64_t value)
    {
        check_sqlite(
            database_,
            "bind integer",
            sqlite3_bind_int64(statement_, index, value));
    }

    void Statement::bind_text(int index, const std::string& value)
    {
        if (value.size() > static_cast<std::size_t>(
                               (std::numeric_limits<int>::max)()))
        {
            throw std::length_error("events: text value is too large");
        }

        const char* data = value.empty() ? "" : value.data();
        check_sqlite(
            database_,
            "bind text",
            sqlite3_bind_text(
                statement_,
                index,
                data,
                static_cast<int>(value.size()),
                SQLITE_TRANSIENT));
    }

    void Statement::bind_optional_text(
        int index,
        const std::optional<std::string>& value)
    {
        if (!value.has_value())
        {
            check_sqlite(
                database_,
                "bind null",
                sqlite3_bind_null(statement_, index));
            return;
        }

        bind_text(index, *value);
    }

    bool Statement::step_row()
    {
        const int result = sqlite3_step(statement_);

        if (result == SQLITE_ROW)
        {
            return true;
        }

        if (result == SQLITE_DONE)
        {
            return false;
        }

        throw_sqlite_error(database_, "step", result);
    }

    void Statement::step_done()
    {
        if (step_row())
        {
            throw std::runtime_error(
                "events: SQLite statement unexpectedly returned a row");
        }
    }

    std::int64_t Statement::column_int64(int column) const
    {
        return sqlite3_column_int64(statement_, column);
    }

    std::string Statement::column_text(int column) const
    {
        const auto* value = sqlite3_column_text(statement_, column);
        const int size = sqlite3_column_bytes(statement_, column);

        if (value == nullptr || size == 0)
        {
            return {};
        }

        return std::string(
            reinterpret_cast<const char*>(value),
            static_cast<std::size_t>(size));
    }

    Transaction::Transaction(sqlite3* database)
        : database_(database)
    {
        execute_sql(database_, "BEGIN IMMEDIATE");
    }

    Transaction::~Transaction()
    {
        if (active_)
        {
            sqlite3_exec(database_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }

    void Transaction::commit()
    {
        execute_sql(database_, "COMMIT");
        active_ = false;
    }
}
