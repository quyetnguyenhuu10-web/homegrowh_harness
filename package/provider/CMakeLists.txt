cmake_minimum_required(VERSION 3.20)

project(provider LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------
# Dependencies - pinned versions
# ---------------------------------------------------------

include(FetchContent)

# libcurl 8.16.0
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
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

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG curl-8_16_0
    GIT_SHALLOW TRUE
)

set(CPR_USE_EXISTING_CURL_TARGET ON CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS_SSL OFF CACHE BOOL "" FORCE)
set(CPR_BUILD_TESTS_PROXY OFF CACHE BOOL "" FORCE)
set(CPR_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.14.2
    GIT_SHALLOW TRUE
)

# nlohmann/json 3.12.0
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(
    curl
    cpr
    nlohmann_json
)

# ---------------------------------------------------------
# Provider library
# ---------------------------------------------------------

add_library(provider
    src/compaction/build_transcript.cpp
    src/compaction/buld_transcript.h
    src/compaction/compaction.cpp
    src/compaction/compaction.h
    src/request/bonsai.h
    src/request/catalog.h
    src/request/completion_detail.h
    src/request/completion_port.cpp
    src/request/deepseek.h
    src/request/openai.h
    src/request/request_state.h
    src/request/requests.cpp
    src/request/sse.cpp
    src/request/usage.h
)

# ---------------------------------------------------------
# Platform implementation
# ---------------------------------------------------------

if(WIN32)

    target_sources(provider
        PRIVATE
            src/platform/windows/request/completion.h
            src/platform/windows/request/completion.cpp
    )

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")

    target_sources(provider
        PRIVATE
            src/platform/linux/request/completion.h
            src/platform/linux/request/completion.cpp
    )

else()

    message(FATAL_ERROR
        "Unsupported operating system: ${CMAKE_SYSTEM_NAME}"
    )

endif()

target_include_directories(provider
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(provider
    PUBLIC
        cpr::cpr
        nlohmann_json::nlohmann_json
)
