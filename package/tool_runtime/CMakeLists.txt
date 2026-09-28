if(NOT TARGET edit_file)
    message(FATAL_ERROR "tool_runtime must be configured by the root CMakeLists.txt")
endif()

set(TOOLS_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
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

add_executable(tool_runtime
    src/tool_runtime/tool_runtime.cpp
    src/tool_runtime/process/process.cpp
    src/tool_runtime/process/process.h
    src/tool_runtime/runtime/execution.cpp
    src/tool_runtime/runtime/runtime.cpp
    src/tool_runtime/runtime/runtime.h
)

add_dependencies(tool_runtime tool_runtime_typescript_tools edit_file)
set_target_properties(tool_runtime PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${HH_EXECUTABLE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${HH_EXECUTABLE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${HH_EXECUTABLE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${HH_EXECUTABLE_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${HH_EXECUTABLE_DIR}"
)
target_compile_features(tool_runtime PRIVATE cxx_std_20)
target_include_directories(tool_runtime PRIVATE src)
target_link_libraries(tool_runtime
    PRIVATE nlohmann_json::nlohmann_json
)

add_custom_command(TARGET tool_runtime POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${TOOLS_PACKAGE_DIR}/dist"
        "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${TOOLS_PACKAGE_DIR}/package.json"
        "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools/package.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${TOOLS_PACKAGE_DIR}/src/tool_definitions.json"
        "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools/tool_definitions.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:edit_file>"
        "$<TARGET_FILE_DIR:tool_runtime>/$<TARGET_FILE_NAME:edit_file>"
    VERBATIM
)
