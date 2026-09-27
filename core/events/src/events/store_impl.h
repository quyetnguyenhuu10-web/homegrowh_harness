#pragma once

#include <events>

#include "repository.h"
#include "../storage/sqlite/database.h"

#include <mutex>

namespace events
{
    struct Store::Impl
    {
        explicit Impl(const std::filesystem::path& database_path)
            : database(database_path),
              repository(database.get())
        {
        }

        detail::SqliteDatabase database;
        detail::EventRepository repository;
        mutable std::mutex mutex;
    };
}