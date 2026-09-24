cmake_minimum_required(VERSION 3.20)

project(events LANGUAGES CXX)

find_package(SQLite3 REQUIRED)

add_library(events
    src/events/store.cpp
    src/events/repository.cpp
    src/events/append.cpp
    src/events/erase.cpp
    src/events/query.cpp
    src/events/insert_after.cpp
    src/storage/sqlite/database.cpp
    src/storage/sqlite/statement.cpp
)

add_library(events::events ALIAS events)

target_compile_features(events PUBLIC cxx_std_20)

target_include_directories(events
    PUBLIC
        include
    PRIVATE
        src
)

target_link_libraries(events
    PUBLIC
        SQLite::SQLite3
)