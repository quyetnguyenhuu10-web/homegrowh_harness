#include "database.h"

#include "statement.h"

#include <stdexcept>
#include <string>

namespace events::detail
{
    namespace
    {
        std::string path_to_utf8(const std::filesystem::path& path)
        {
            const std::u8string encoded = path.u8string();
            std::string result;
            result.reserve(encoded.size());

            for (const char8_t byte : encoded)
            {
                result.push_back(static_cast<char>(byte));
            }

            return result;
        }
    }

    SqliteDatabase::SqliteDatabase(
        const std::filesystem::path& database_path)
    {
        const std::string path = path_to_utf8(database_path);

        if (path.empty())
        {
            throw std::invalid_argument(
                "events: database path must not be empty");
        }

        if (path != ":memory:" && !path.starts_with("file:"))
        {
            const std::filesystem::path parent = database_path.parent_path();

            if (!parent.empty())
            {
                std::filesystem::create_directories(parent);
            }
        }

        const int result = sqlite3_open_v2(
            path.c_str(),
            &database_,
            SQLITE_OPEN_READWRITE |
                SQLITE_OPEN_CREATE |
                SQLITE_OPEN_FULLMUTEX |
                SQLITE_OPEN_URI,
            nullptr);

        if (result != SQLITE_OK)
        {
            const std::string detail = database_ == nullptr
                ? "unknown SQLite error"
                : sqlite3_errmsg(database_);

            if (database_ != nullptr)
            {
                sqlite3_close_v2(database_);
                database_ = nullptr;
            }

            throw std::runtime_error(
                "events: SQLite open failed (" +
                std::to_string(result) + "): " + detail);
        }

        try
        {
            check_sqlite(
                database_,
                "enable extended result codes",
                sqlite3_extended_result_codes(database_, 1));
            check_sqlite(
                database_,
                "set busy timeout",
                sqlite3_busy_timeout(database_, 5000));
        }
        catch (...)
        {
            sqlite3_close_v2(database_);
            database_ = nullptr;
            throw;
        }
    }

    SqliteDatabase::~SqliteDatabase()
    {
        if (database_ != nullptr)
        {
            sqlite3_close_v2(database_);
        }
    }

    sqlite3* SqliteDatabase::get() const noexcept
    {
        return database_;
    }
}