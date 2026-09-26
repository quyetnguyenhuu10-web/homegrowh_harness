include_guard(GLOBAL)

# All third-party sources and binaries live in the root build tree.
set(HH_NLOHMANN_JSON_VERSION 3.12.0)
set(HH_CLI11_VERSION 2.6.1)
set(HH_CURL_GIT_TAG curl-8_16_0)
set(HH_CPR_VERSION 1.14.2)
set(HH_SQLITE_AMALGAMATION_VERSION 3530400)

get_filename_component(HH_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(HH_EXECUTABLE_DIR "${HH_REPOSITORY_ROOT}/executable"
    CACHE PATH "Directory for runnable Homegrowh executables")

function(hh_add_dependencies)
    include(FetchContent)
    find_package(Threads REQUIRED)

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    set(CURL_BROTLI OFF CACHE STRING "" FORCE)
    set(CURL_ZSTD OFF CACHE STRING "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    endif()

    set(CPR_USE_EXISTING_CURL_TARGET ON CACHE BOOL "" FORCE)
    set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(CPR_BUILD_TESTS_SSL OFF CACHE BOOL "" FORCE)
    set(CPR_BUILD_TESTS_PROXY OFF CACHE BOOL "" FORCE)
    set(CPR_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG "v${HH_NLOHMANN_JSON_VERSION}"
        GIT_SHALLOW TRUE)
    FetchContent_Declare(cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG "v${HH_CLI11_VERSION}"
        GIT_SHALLOW TRUE)
    FetchContent_Declare(curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG "${HH_CURL_GIT_TAG}"
        GIT_SHALLOW TRUE)
    FetchContent_Declare(cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG "${HH_CPR_VERSION}"
        GIT_SHALLOW TRUE)
    FetchContent_Declare(sqlite_amalgamation
        URL "https://www.sqlite.org/2026/sqlite-amalgamation-${HH_SQLITE_AMALGAMATION_VERSION}.zip"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

    FetchContent_MakeAvailable(
        nlohmann_json cli11 curl cpr sqlite_amalgamation)

    add_library(hh_sqlite3 STATIC "${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c")
    add_library(SQLite::SQLite3 ALIAS hh_sqlite3)
    target_include_directories(hh_sqlite3 PUBLIC
        "${sqlite_amalgamation_SOURCE_DIR}")
    target_link_libraries(hh_sqlite3 PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
endfunction()

function(hh_require_target name)
    if(NOT TARGET "${name}")
        message(FATAL_ERROR
            "Missing shared target ${name}. Configure from the repository root.")
    endif()
endfunction()

function(hh_require_threads)
    hh_require_target(Threads::Threads)
endfunction()

function(hh_require_nlohmann_json)
    hh_require_target(nlohmann_json::nlohmann_json)
endfunction()

function(hh_require_cli11)
    hh_require_target(CLI11::CLI11)
endfunction()

function(hh_require_http)
    hh_require_target(CURL::libcurl)
    hh_require_target(cpr::cpr)
endfunction()

function(hh_require_sqlite)
    hh_require_target(SQLite::SQLite3)
endfunction()

function(hh_require_all_dependencies)
    hh_require_threads()
    hh_require_nlohmann_json()
    hh_require_cli11()
    hh_require_http()
    hh_require_sqlite()
endfunction()
