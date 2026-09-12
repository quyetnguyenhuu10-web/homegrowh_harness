cmake_minimum_required(VERSION 3.20)

project(filesystems LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(filesystems
    src/fsysteam/read/reader.cpp
    src/fsysteam/edit/edit.cpp
)

# ---------------------------------------------------------
# Platform implementation
# ---------------------------------------------------------

if(WIN32)

    target_sources(filesystems
        PRIVATE
            src/platform/windows/fsysteam/reader/windows_reader.cpp
            src/platform/windows/fsysteam/edit/windows_edit.cpp
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    target_sources(filesystems
        PRIVATE
            src/platform/linux/fsysteam/reader/linux_reader.cpp
            src/platform/linux/fsysteam/edit/linux_edit.cpp
    )

    target_compile_definitions(filesystems
        PRIVATE
            _POSIX_C_SOURCE=200809L
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

    target_link_libraries(file_reader
        PRIVATE
            filesystems
    )

    target_link_libraries(file_edit
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

endif()
