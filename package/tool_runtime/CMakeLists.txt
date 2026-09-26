if(NOT TARGET sandbox OR NOT TARGET edit_file)
    message(FATAL_ERROR "tool_runtime must be configured by the root CMakeLists.txt")
endif()

set(TOOLS_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
set(TOOL_HOST_PATH "${TOOLS_PACKAGE_DIR}/dist/src/_runtime/tool_host.js")

find_program(NPM_EXECUTABLE NAMES npm npm.cmd)
if(NOT NPM_EXECUTABLE)
    message(FATAL_ERROR "npm is required to build the TypeScript tool worker")
endif()

add_custom_target(tool_runtime_typescript_tools
    COMMAND "${NPM_EXECUTABLE}" run build
    WORKING_DIRECTORY "${TOOLS_PACKAGE_DIR}"
    COMMENT "Building TypeScript tool worker"
    VERBATIM
)

add_library(tool_runtime
    src/tool_runtime/execute/execute.cpp
    src/tool_runtime/execute/execute.h
    src/tool_runtime/runtime/runtime.cpp
    src/tool_runtime/runtime/runtime.h
)
add_library(tool_runtime::tool_runtime ALIAS tool_runtime)

add_dependencies(tool_runtime tool_runtime_typescript_tools edit_file)
target_compile_features(tool_runtime PUBLIC cxx_std_20)
target_include_directories(tool_runtime PUBLIC include src)
target_compile_definitions(tool_runtime PRIVATE
    TOOL_RUNTIME_TOOL_HOST_PATH="${TOOL_HOST_PATH}"
    TOOL_RUNTIME_EDIT_FILE_PATH="$<TARGET_FILE:edit_file>"
)
target_link_libraries(tool_runtime
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE sandbox
)

option(TOOL_RUNTIME_BUILD_TESTS "Build tool_runtime tests" OFF)
if(TOOL_RUNTIME_BUILD_TESTS)
    add_executable(tool_runtime_test tests/runtime_test.cpp)
    target_link_libraries(tool_runtime_test PRIVATE tool_runtime)
endif()
