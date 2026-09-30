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
set(PROVIDER_TYPES_SOURCE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/provider_types.generated.cpp"
)

add_executable(provider_types_codegen
    codegen/schema.cpp
    codegen/schema.h
    codegen/emit_usage_parser.cpp
    codegen/emit_usage_parser.h
    codegen/generate_provider_types.cpp
    src/error/capture.cpp
    src/error/error.cpp
)

target_link_libraries(provider_types_codegen
    PRIVATE
        nlohmann_json::nlohmann_json
)
target_compile_features(provider_types_codegen PRIVATE cxx_std_20)

add_custom_command(
    OUTPUT "${PROVIDER_TYPES_HEADER}" "${PROVIDER_TYPES_SOURCE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "$<TARGET_FILE:provider_types_codegen>"
        "${PROVIDER_TYPES_JSON}"
        "${PROVIDER_TYPES_HEADER}"
        "${PROVIDER_TYPES_SOURCE}"
    DEPENDS
        provider_types_codegen
        "${PROVIDER_TYPES_JSON}"
    VERBATIM
)

add_custom_target(generate_provider_types
    DEPENDS "${PROVIDER_TYPES_HEADER}" "${PROVIDER_TYPES_SOURCE}"
)

# ---------------------------------------------------------
# Provider library
# ---------------------------------------------------------

add_library(provider
    "${PROVIDER_TYPES_HEADER}"
    "${PROVIDER_TYPES_SOURCE}"
    src/error/capture.cpp
    src/error/error.cpp
    src/compaction/build_transcript.cpp
    src/compaction/buld_transcript.h
    src/compaction/compaction.cpp
    src/compaction/compaction.h
    src/request/request_state.h
    src/request/request_state.cpp
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
target_compile_features(provider PUBLIC cxx_std_20)

option(PROVIDER_BUILD_TESTS "Build provider tests" ${HH_BUILD_TESTS})
if(PROVIDER_BUILD_TESTS)
    enable_testing()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_executable(provider_error_schema_test tests/error_schema_test.cpp)
    target_link_libraries(provider_error_schema_test PRIVATE provider)
    add_executable(provider_call_llm tests/call_llm.cpp)
    target_link_libraries(provider_call_llm PRIVATE provider)
    add_test(NAME provider_errors
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_error_tests.py"
            "$<TARGET_FILE:provider_error_schema_test>"
            "$<TARGET_FILE:provider_types_codegen>")
    set_tests_properties(provider_errors PROPERTIES TIMEOUT 90)
endif()
