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
    src/config/edit_config.h
    src/config/reader_config.h
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
            src/platform/windows/fsystem/edit/detail/edit_context.h
            src/platform/windows/fsystem/edit/detail/windows_handle.h
            src/platform/windows/fsystem/edit/detail/source_stream.h
            src/platform/windows/fsystem/edit/detail/source_stream.cpp
            src/platform/windows/fsystem/edit/detail/temp_writer.h
            src/platform/windows/fsystem/edit/detail/temp_writer.cpp
            src/platform/windows/fsystem/edit/detail/replace_transaction.h
            src/platform/windows/fsystem/edit/detail/replace_transaction.cpp
            src/platform/windows/fsystem/watcher/windows_watcher.cpp
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    target_sources(filesystems
        PRIVATE
            src/platform/linux/fsystem/reader/linux_reader.cpp
            src/platform/linux/fsystem/edit/linux_edit.cpp
            src/platform/linux/fsystem/edit/detail/edit_context.h
            src/platform/linux/fsystem/edit/detail/linux_handle.h
            src/platform/linux/fsystem/edit/detail/source_stream.h
            src/platform/linux/fsystem/edit/detail/source_stream.cpp
            src/platform/linux/fsystem/edit/detail/temp_writer.h
            src/platform/linux/fsystem/edit/detail/temp_writer.cpp
            src/platform/linux/fsystem/edit/detail/replace_transaction.h
            src/platform/linux/fsystem/edit/detail/replace_transaction.cpp
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

set(FILESYSTEMS_READER_BLOCK_SIZE
    4096
    CACHE STRING "Reader block size in bytes"
)

if(NOT "${FILESYSTEMS_READER_BLOCK_SIZE}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "FILESYSTEMS_READER_BLOCK_SIZE must be a positive integer"
    )
endif()

if(
    FILESYSTEMS_READER_BLOCK_SIZE LESS 1 OR
    FILESYSTEMS_READER_BLOCK_SIZE GREATER 4294967295
)
    message(FATAL_ERROR
        "FILESYSTEMS_READER_BLOCK_SIZE must fit in a uint32_t"
    )
endif()

set(FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE
    65536
    CACHE STRING "Edit stream chunk size in bytes"
)

if(NOT "${FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE}" MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR
        "FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE must be a positive integer"
    )
endif()

if(
    FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE LESS 1 OR
    FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE GREATER 4294967295
)
    message(FATAL_ERROR
        "FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE must fit in a DWORD"
    )
endif()

target_compile_definitions(filesystems
    PRIVATE
        FILESYSTEMS_READER_BLOCK_SIZE=${FILESYSTEMS_READER_BLOCK_SIZE}
        FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE=${FILESYSTEMS_EDIT_STREAM_CHUNK_SIZE}
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

        set(FILESYSTEMS_WINDOWS_EDIT_STRESS_DIR
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/windows/fsystem/edit/windows_edit_stress
        )

        set(FILESYSTEMS_WINDOWS_EDIT_STRESS_COMPONENTS
            windows_edit_stress_bench
        )

        set(FILESYSTEMS_WINDOWS_EDIT_STRESS_SOURCES
            bench/bench.cpp
        )

        list(LENGTH FILESYSTEMS_WINDOWS_EDIT_STRESS_COMPONENTS
            FILESYSTEMS_WINDOWS_EDIT_STRESS_COMPONENT_COUNT
        )

        math(EXPR FILESYSTEMS_WINDOWS_EDIT_STRESS_LAST_INDEX
            "${FILESYSTEMS_WINDOWS_EDIT_STRESS_COMPONENT_COUNT} - 1"
        )

        foreach(index RANGE ${FILESYSTEMS_WINDOWS_EDIT_STRESS_LAST_INDEX})
            list(GET FILESYSTEMS_WINDOWS_EDIT_STRESS_COMPONENTS
                ${index}
                component
            )
            list(GET FILESYSTEMS_WINDOWS_EDIT_STRESS_SOURCES
                ${index}
                source
            )

            add_executable(${component}
                ${FILESYSTEMS_WINDOWS_EDIT_STRESS_DIR}/${source}
                ${FILESYSTEMS_WINDOWS_EDIT_STRESS_DIR}/common/stress_support.h
                ${FILESYSTEMS_WINDOWS_EDIT_STRESS_DIR}/common/bench_cases.h
            )

            target_link_libraries(${component}
                PRIVATE
                    filesystems
                    Psapi
                    nlohmann_json::nlohmann_json
            )
        endforeach()

        find_program(FILESYSTEMS_NODE_EXECUTABLE
            NAMES node node.exe
        )

        if(NOT FILESYSTEMS_NODE_EXECUTABLE)
            message(FATAL_ERROR
                "Node.js is required for the Windows edit stress reports"
            )
        endif()

    endif()

    if(
        CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
        FILESYSTEMS_BUILD_LINUX_STRESS_TESTS
    )

        set(FILESYSTEMS_LINUX_EDIT_STRESS_DIR
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/linux/fsystem/edit/linux_edit_stress
        )

        set(FILESYSTEMS_LINUX_EDIT_STRESS_COMPONENTS
            linux_edit_stress_bench
        )

        set(FILESYSTEMS_LINUX_EDIT_STRESS_SOURCES
            bench/bench.cpp
        )

        list(LENGTH FILESYSTEMS_LINUX_EDIT_STRESS_COMPONENTS
            FILESYSTEMS_LINUX_EDIT_STRESS_COMPONENT_COUNT
        )

        math(EXPR FILESYSTEMS_LINUX_EDIT_STRESS_LAST_INDEX
            "${FILESYSTEMS_LINUX_EDIT_STRESS_COMPONENT_COUNT} - 1"
        )

        foreach(index RANGE ${FILESYSTEMS_LINUX_EDIT_STRESS_LAST_INDEX})
            list(GET FILESYSTEMS_LINUX_EDIT_STRESS_COMPONENTS
                ${index}
                component
            )
            list(GET FILESYSTEMS_LINUX_EDIT_STRESS_SOURCES
                ${index}
                source
            )

            add_executable(${component}
                ${FILESYSTEMS_LINUX_EDIT_STRESS_DIR}/${source}
                ${FILESYSTEMS_LINUX_EDIT_STRESS_DIR}/common/stress_support.h
                ${FILESYSTEMS_LINUX_EDIT_STRESS_DIR}/common/bench_cases.h
            )

            target_link_libraries(${component}
                PRIVATE
                    filesystems
                    nlohmann_json::nlohmann_json
            )

            target_compile_definitions(${component}
                PRIVATE
                    _POSIX_C_SOURCE=200809L
                    _GNU_SOURCE
            )
        endforeach()

        find_program(FILESYSTEMS_NODE_EXECUTABLE
            NAMES node nodejs node.exe
        )

        if(NOT FILESYSTEMS_NODE_EXECUTABLE)
            message(FATAL_ERROR
                "Node.js is required for the Linux edit stress reports"
            )
        endif()

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

        set(FILESYSTEMS_WINDOWS_EDIT_STRESS_RUNNER
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/windows/fsystem/edit/windows_edit_stress/run_all.js
        )

        add_test(
            NAME windows_edit_stress_smoke_test
            COMMAND ${FILESYSTEMS_NODE_EXECUTABLE}
                ${FILESYSTEMS_WINDOWS_EDIT_STRESS_RUNNER}
                --profile smoke
                --build-dir
                ${CMAKE_CURRENT_BINARY_DIR}
        )

        add_test(
            NAME windows_edit_stress_standard_test
            COMMAND ${FILESYSTEMS_NODE_EXECUTABLE}
                ${FILESYSTEMS_WINDOWS_EDIT_STRESS_RUNNER}
                --profile standard
                --build-dir
                ${CMAKE_CURRENT_BINARY_DIR}
        )

        add_test(
            NAME windows_edit_stress_extreme_test
            COMMAND ${FILESYSTEMS_NODE_EXECUTABLE}
                ${FILESYSTEMS_WINDOWS_EDIT_STRESS_RUNNER}
                --profile extreme
                --build-dir
                ${CMAKE_CURRENT_BINARY_DIR}
        )

        set_tests_properties(
            windows_edit_stress_smoke_test
            windows_edit_stress_standard_test
            windows_edit_stress_extreme_test
            PROPERTIES
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

    endif()

    if(
        CMAKE_SYSTEM_NAME STREQUAL "Linux" AND
        FILESYSTEMS_BUILD_LINUX_STRESS_TESTS
    )

        set(FILESYSTEMS_LINUX_EDIT_STRESS_RUNNER
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/platform/linux/fsystem/edit/linux_edit_stress/run_all.js
        )

        add_test(
            NAME linux_edit_stress_smoke_test
            COMMAND ${FILESYSTEMS_NODE_EXECUTABLE}
                ${FILESYSTEMS_LINUX_EDIT_STRESS_RUNNER}
                --profile smoke
                --build-dir
                ${CMAKE_CURRENT_BINARY_DIR}
        )

        add_test(
            NAME linux_edit_stress_standard_test
            COMMAND ${FILESYSTEMS_NODE_EXECUTABLE}
                ${FILESYSTEMS_LINUX_EDIT_STRESS_RUNNER}
                --profile standard
                --build-dir
                ${CMAKE_CURRENT_BINARY_DIR}
        )

        add_test(
            NAME linux_edit_stress_extreme_test
            COMMAND ${FILESYSTEMS_NODE_EXECUTABLE}
                ${FILESYSTEMS_LINUX_EDIT_STRESS_RUNNER}
                --profile extreme
                --build-dir
                ${CMAKE_CURRENT_BINARY_DIR}
        )

        set_tests_properties(
            linux_edit_stress_smoke_test
            linux_edit_stress_standard_test
            linux_edit_stress_extreme_test
            PROPERTIES
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )

    endif()

endif()
