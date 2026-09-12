cmake_minimum_required(VERSION 3.20)

project(file_reader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# ============================================================
# Dependencies
# ============================================================

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v6.1.9
)

FetchContent_MakeAvailable(ftxui)

# ============================================================
# Filesystem Library
# ============================================================

add_library(filesystems
    src/file/reader.cpp
    src/file/edit.cpp
)

if(WIN32)

    target_sources(filesystems
        PRIVATE
            src/platform/windows/file/reader/windows_reader.cpp
            src/platform/windows/file/edit/windows_edit.cpp
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    target_sources(filesystems
        PRIVATE
            src/platform/linux/file/reader/linux_reader.cpp
            src/platform/linux/file/edit/linux_edit.cpp
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

target_include_directories(filesystems
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include

    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ============================================================
# Test
# ============================================================

option(
    FILESYSTEMS_BUILD_TESTS
    "Build the filesystem test executables"
    ON
)

if(FILESYSTEMS_BUILD_TESTS)

    enable_testing()

    # --------------------------------------------------------
    # Reader test
    # --------------------------------------------------------

    add_executable(file_reader
        tests/main_test.cpp
    )

    target_link_libraries(file_reader
        PRIVATE
            filesystems
            ftxui::component
            ftxui::dom
            ftxui::screen
    )

    add_test(
        NAME file_reader_test
        COMMAND file_reader
    )

    set_tests_properties(file_reader_test
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

    # --------------------------------------------------------
    # Edit test
    # --------------------------------------------------------

    add_executable(file_edit
        tests/edit_file.cpp
    )

    target_link_libraries(file_edit
        PRIVATE
            filesystems
    )

    add_test(
        NAME file_edit_test
        COMMAND file_edit
    )

    set_tests_properties(file_edit_test
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )

endif()