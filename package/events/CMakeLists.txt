if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(events
    src/events/events.h
    src/events/create.cpp
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
        src
)

target_link_libraries(events
    PUBLIC
        SQLite::SQLite3
)
