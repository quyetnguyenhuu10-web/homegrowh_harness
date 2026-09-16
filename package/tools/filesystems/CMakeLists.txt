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
    src/fsystem/read/reader.cpp
    src/fsystem/edit/edit.cpp
    src/fsystem/watcher/watcher.cpp
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
            src/platform/linux/fsystem/reader/linux_reader.cpp
            src/platform/linux/fsystem/edit/linux_edit.cpp
            src/platform/linux/fsystem/watcher/linux_watcher.cpp
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

target_link_libraries(filesystems
    PUBLIC
        Threads::Threads
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

option(FILESYSTEMS_BUILD_WINDOWS_STRESS_TESTS
    "Build the Windows edit stress harness"
    OFF
)

option(FILESYSTEMS_BUILD_LINUX_STRESS_TESTS
    "Build the Linux edit stress harness"
    OFF
)

if(FILESYSTEMS_BUILD_TESTS)

    enable_testing()

    set(FILESYSTEMS_TEST_INCLUDE_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/include
    )

    add_executable(file_reader
        tests/cross_platform/fsystem/read/reader_test.cpp
    )

    add_executable(file_edit
        tests/cross_platform/fsystem/edit/edit_test.cpp
    )

    add_executable(watch_file
        tests/cross_platform/fsystem/watcher/watcher_test.cpp
    )

    add_executable(chunk_matcher_stress
        tests/cross_platform/fsystem/edit/chunk_matcher_stress.cpp
    )

    target_include_directories(file_reader
        PRIVATE
            ${FILESYSTEMS_TEST_INCLUDE_DIR}
    )

    target_include_directories(file_edit
        PRIVATE
            ${FILESYSTEMS_TEST_INCLUDE_DIR}
    )

    target_include_directories(watch_file
        PRIVATE
            ${FILESYSTEMS_TEST_INCLUDE_DIR}
    )

    if(WIN32 AND FILESYSTEMS_BUILD_WINDOWS_STRESS_TESTS)

        add_executable(windows_edit_stress
            tests/platform/windows/fsystem/edit/windows_edit_stress.cpp
        )

        target_link_libraries(windows_edit_stress
            PRIVATE
                filesystems
                Psapi
        )

    endif()

    if(
        CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
        FILESYSTEMS_BUILD_LINUX_STRESS_TESTS
    )

        add_executable(linux_edit_stress
            tests/platform/linux/fsystem/edit/linux_edit_stress.cpp
        )

        target_link_libraries(linux_edit_stress
            PRIVATE
                filesystems
        )

    endif()

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

    target_link_libraries(chunk_matcher_stress
        PRIVATE
            filesystems
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

    add_test(
        NAME chunk_matcher_stress_test
        COMMAND chunk_matcher_stress
    )

    if(WIN32 AND FILESYSTEMS_BUILD_WINDOWS_STRESS_TESTS)

        add_test(
            NAME windows_edit_stress_smoke_test
            COMMAND windows_edit_stress
                --profile smoke
                --output
                ${CMAKE_CURRENT_BINARY_DIR}/windows-edit-stress-results
        )

        add_test(
            NAME windows_edit_stress_standard_test
            COMMAND windows_edit_stress
                --profile standard
                --output
                ${CMAKE_CURRENT_BINARY_DIR}/windows-edit-stress-standard-results
        )

        add_test(
            NAME windows_edit_stress_extreme_test
            COMMAND windows_edit_stress
                --profile extreme
                --output
                ${CMAKE_CURRENT_BINARY_DIR}/windows-edit-stress-extreme-results
        )

    endif()

    if(
        CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
        FILESYSTEMS_BUILD_LINUX_STRESS_TESTS
    )

        add_test(
            NAME linux_edit_stress_smoke_test
            COMMAND linux_edit_stress
                --profile smoke
                --output
                ${CMAKE_CURRENT_BINARY_DIR}/linux-edit-stress-results
        )

        add_test(
            NAME linux_edit_stress_standard_test
            COMMAND linux_edit_stress
                --profile standard
                --output
                ${CMAKE_CURRENT_BINARY_DIR}/linux-edit-stress-standard-results
        )

        add_test(
            NAME linux_edit_stress_extreme_test
            COMMAND linux_edit_stress
                --profile extreme
                --output
                ${CMAKE_CURRENT_BINARY_DIR}/linux-edit-stress-extreme-results
        )

    endif()

endif()
