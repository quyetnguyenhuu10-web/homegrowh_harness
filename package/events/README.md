# Events

The events package is a standalone C++20 SQLite library, organized like the provider package. It has no executable target and no platform-specific implementation because SQLite provides the cross-platform storage API.

The public interface is the umbrella header include/events. Event API and schema-specific SQL live under src/events; SQLite connection and statement/transaction support live under src/storage/sqlite.

The table columns are row_position, events, create_at, Session_ID, provider, and model. Positions are zero-based and dense. append adds at the end; insert_after inserts after an existing position and shifts later rows; erase removes a row and closes the gap. query() returns every row in position order, and query(session_id) filters by session. If create_at is omitted, SQLite supplies the current UTC timestamp.

Add package/events with CMake and link consumers against events::events. SQLite development files must be available to CMake through find_package(SQLite3 REQUIRED).

C++ reserves delete, so the corresponding API is named erase.