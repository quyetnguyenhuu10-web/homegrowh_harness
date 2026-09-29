if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

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
        "${PROVIDER_TYPES_HEADER}"
    DEPENDS
        provider_types_codegen
        "${PROVIDER_TYPES_JSON}"
    VERBATIM
)

add_custom_target(generate_provider_types
    DEPENDS "${PROVIDER_TYPES_HEADER}"
)

# ---------------------------------------------------------
# Provider library
# ---------------------------------------------------------

add_library(provider
    "${PROVIDER_TYPES_HEADER}"
    src/compaction/build_transcript.cpp
    src/compaction/buld_transcript.h
    src/compaction/compaction.cpp
    src/compaction/compaction.h
    src/request/request_state.h
    src/request/requests.cpp
    src/request/sse.cpp
    src/stream/stream.cpp
    src/stream/stream.h
)

add_dependencies(
    provider
    generate_provider_types
)

target_include_directories(provider
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/generated
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(provider
    PUBLIC
        cpr::cpr
        nlohmann_json::nlohmann_json
)
