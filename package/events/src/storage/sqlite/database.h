#pragma once

#include <sqlite3.h>

#include <filesystem>

namespace events::detail
{
    class SqliteDatabase
    {
    public:
        explicit SqliteDatabase(const std::filesystem::path& database_path);
        ~SqliteDatabase();

        SqliteDatabase(const SqliteDatabase&) = delete;
        SqliteDatabase& operator=(const SqliteDatabase&) = delete;

        sqlite3* get() const noexcept;

    private:
        sqlite3* database_ = nullptr;
    };
}