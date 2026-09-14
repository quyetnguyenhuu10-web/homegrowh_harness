cmake_minimum_required(VERSION 3.20)

project(filesystems LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

# ---------------------------------------------------------
# Dependencies
# ---------------------------------------------------------

include(FetchContent)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.6.1
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
)

FetchContent_MakeAvailable(
    CLI11
    nlohmann_json
)

# ---------------------------------------------------------
# Library
# ---------------------------------------------------------

add_library(filesystems
    src/fsysteam/read/reader.cpp
    src/fsysteam/edit/edit.cpp
    src/fsysteam/watcher/watcher.cpp
)

# ---------------------------------------------------------
# Platform implementation
# ---------------------------------------------------------

if(WIN32)

    target_sources(filesystems
        PRIVATE
            src/platform/windows/fsystem/reader/windows_reader.cpp
            src/platform/windows/fsystem/edit/windows_edit.cpp
            src/platform/windows/fsystem/watcher/windows_watcher.cpp
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    target_sources(filesystems
        PRIVATE
            src/platform/linux/fsysteam/reader/linux_reader.cpp
            src/platform/linux/fsysteam/edit/linux_edit.cpp
            src/platform/linux/fsysteam/watcher/linux_watcher.cpp
    )

    target_compile_definitions(filesystems
        PRIVATE
            _POSIX_C_SOURCE=200809L
            _GNU_SOURCE
    )

else()

    message(FATAL_ERROR
        "Unsupported operating system: ${CMAKE_SYSTEM_NAME}"
    )

endif()

# ---------------------------------------------------------
# Include directories
# ---------------------------------------------------------

target_include_directories(filesystems
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ---------------------------------------------------------
# Executables
# ---------------------------------------------------------

add_executable(edit_file
    executables/edit_file.cpp
)

target_link_libraries(edit_file
    PRIVATE
        filesystems
        CLI11::CLI11
        nlohmann_json::nlohmann_json
)

# ---------------------------------------------------------
# Tests
# ---------------------------------------------------------

option(FILESYSTEMS_BUILD_TESTS
    "Build filesystem tests"
    ON
)

if(FILESYSTEMS_BUILD_TESTS)

    enable_testing()

    add_executable(file_reader
        tests/main_test.cpp
    )

    add_executable(file_edit
        tests/edit_file.cpp
    )

    add_executable(watch_file
        tests/watch_file.cpp
    )

    target_link_libraries(file_reader
        PRIVATE
            filesystems
    )

    target_link_libraries(file_edit
        PRIVATE
            filesystems
    )

    target_link_libraries(watch_file
        PRIVATE
            filesystems
            Threads::Threads
    )

    add_test(
        NAME file_reader_test
        COMMAND file_reader
    )

    add_test(
        NAME file_edit_test
        COMMAND file_edit
    )

    add_test(
        NAME watch_file_test
        COMMAND watch_file
    )

endif()
