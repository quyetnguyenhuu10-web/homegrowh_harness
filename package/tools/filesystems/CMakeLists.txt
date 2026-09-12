cmake_minimum_required(VERSION 3.20)

project(json_file_reader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# Filesystem Library
# ============================================================

add_library(filesystems
    src/file/reader.cpp
)

if(WIN32)
    target_sources(filesystems
        PRIVATE
            src/platform/windows/windows_reader.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(filesystems
        PRIVATE
            src/platform/linux/linux_reader.cpp
    )
    target_compile_definitions(filesystems
        PRIVATE
            _POSIX_C_SOURCE=200809L
    )
else()
    message(FATAL_ERROR "Unsupported operating system: ${CMAKE_SYSTEM_NAME}")
endif()

target_include_directories(filesystems
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================
# Test
# ============================================================

option(FILESYSTEMS_BUILD_TESTS "Build the filesystem reader test executable" ON)

if(FILESYSTEMS_BUILD_TESTS)
    enable_testing()

    add_executable(json_file_reader
        tests/main_test.cpp
    )

    target_link_libraries(json_file_reader
        PRIVATE
            filesystems
    )

    add_test(
        NAME json_file_reader_test
        COMMAND json_file_reader
    )

    set_tests_properties(json_file_reader_test
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
endif()
