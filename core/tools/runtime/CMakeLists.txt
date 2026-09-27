if(NOT TARGET edit_file)
    message(FATAL_ERROR "tool_runtime must be configured by the root CMakeLists.txt")
endif()

set(TOOLS_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

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
    src/tool_runtime/process/process.cpp
    src/tool_runtime/process/process.h
    src/tool_runtime/runtime/runtime.cpp
    src/tool_runtime/runtime/runtime.h
)
add_library(tool_runtime::tool_runtime ALIAS tool_runtime)

add_dependencies(tool_runtime tool_runtime_typescript_tools edit_file)
target_compile_features(tool_runtime PUBLIC cxx_std_20)
target_include_directories(tool_runtime PUBLIC include src)
target_link_libraries(tool_runtime
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE Threads::Threads
)

if(WIN32)
    target_sources(tool_runtime PRIVATE
        src/tool_runtime/process/platform/windows.cpp
        src/tool_runtime/process/platform/windows.h
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tool_runtime PRIVATE
        src/tool_runtime/process/platform/linux.cpp
        src/tool_runtime/process/platform/linux.h
    )
endif()

add_executable(tool_runtime_executable
    executable/main.cpp
)
set_target_properties(tool_runtime_executable PROPERTIES OUTPUT_NAME tool_runtime)
target_link_libraries(tool_runtime_executable PRIVATE tool_runtime)
target_compile_features(tool_runtime_executable PRIVATE cxx_std_20)

option(TOOL_RUNTIME_BUILD_TESTS "Build tool_runtime tests" OFF)
if(TOOL_RUNTIME_BUILD_TESTS)
    add_executable(tool_runtime_test tests/runtime_test.cpp)
    target_link_libraries(tool_runtime_test PRIVATE tool_runtime)
endif()
