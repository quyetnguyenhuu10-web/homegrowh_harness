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
# Types generated from the provider schema
# ---------------------------------------------------------

set(PROVIDER_TYPES_JSON
    "${CMAKE_CURRENT_SOURCE_DIR}/src/request/provider_types.json"
)
set(PROVIDER_TYPES_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/provider_types.generated.h"
)

add_executable(provider_types_codegen
    codegen/emit_usage_parser.cpp
    codegen/emit_usage_parser.h
    codegen/generate_provider_types.cpp
)

target_link_libraries(provider_types_codegen
    PRIVATE
        nlohmann_json::nlohmann_json
)

add_custom_command(
    OUTPUT "${PROVIDER_TYPES_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "$<TARGET_FILE:provider_types_codegen>"
        "${PROVIDER_TYPES_JSON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json"
        "${PROVIDER_TYPES_HEADER}"
    DEPENDS
        provider_types_codegen
        "${PROVIDER_TYPES_JSON}"
        "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json"
    VERBATIM
)

add_custom_target(generate_provider_types
    DEPENDS "${PROVIDER_TYPES_HEADER}"
)

# ---------------------------------------------------------
# Context usage generated from provider formulas
# ---------------------------------------------------------

set(CONTEXT_USAGE_JSON
    "${CMAKE_CURRENT_SOURCE_DIR}/src/context_usage/context_usage.json"
)
set(CONTEXT_USAGE_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/context_usage.generated.h"
)

add_executable(context_usage_codegen
    codegen/generate_context_usage.cpp
)

target_link_libraries(context_usage_codegen
    PRIVATE
        nlohmann_json::nlohmann_json
)

add_custom_command(
    OUTPUT "${CONTEXT_USAGE_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "$<TARGET_FILE:context_usage_codegen>"
        "${CONTEXT_USAGE_JSON}"
        "${PROVIDER_TYPES_JSON}"
        "${CONTEXT_USAGE_HEADER}"
    DEPENDS
        context_usage_codegen
        "${CONTEXT_USAGE_JSON}"
        "${PROVIDER_TYPES_JSON}"
    VERBATIM
)

add_custom_target(generate_context_usage
    DEPENDS "${CONTEXT_USAGE_HEADER}"
)

# ---------------------------------------------------------
# Provider library
# ---------------------------------------------------------

add_library(provider
    "${PROVIDER_TYPES_HEADER}"
    "${CONTEXT_USAGE_HEADER}"
    src/compaction/build_transcript.cpp
    src/compaction/buld_transcript.h
    src/compaction/compaction.cpp
    src/compaction/compaction.h
    src/context_usage/context_usage.cpp
    src/context_usage/context_usage.h
    src/request/catalog.h
    src/request/completion_detail.h
    src/request/completion_port.cpp
    src/request/request_state.h
    src/request/requests.cpp
    src/request/sse.cpp
)

add_dependencies(
    provider
    generate_provider_types
    generate_context_usage
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
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(provider
    PUBLIC
        cpr::cpr
        nlohmann_json::nlohmann_json
)
