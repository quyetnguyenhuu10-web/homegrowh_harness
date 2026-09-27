cmake_minimum_required(VERSION 3.20)

project(homegrowh_harness LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# One dependency file owns pinned third-party targets in this build tree.
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake")
hh_add_dependencies()
hh_require_all_dependencies()

option(HH_BUILD_TESTS "Build core tests" OFF)
set(EVENT_PORT_BUILD_TESTS ${HH_BUILD_TESTS} CACHE BOOL "" FORCE)
set(SECRETS_BUILD_TESTS ${HH_BUILD_TESTS} CACHE BOOL "" FORCE)
set(SANDBOX_BUILD_TESTS ${HH_BUILD_TESTS} CACHE BOOL "" FORCE)
set(PROFILE_SANDBOX_BUILD_TESTS ${HH_BUILD_TESTS} CACHE BOOL "" FORCE)
set(FILESYSTEMS_BUILD_TESTS ${HH_BUILD_TESTS} CACHE BOOL "" FORCE)
set(TOOL_RUNTIME_BUILD_TESTS ${HH_BUILD_TESTS} CACHE BOOL "" FORCE)
set(SESSIONS_BUILD_TESTS ${HH_BUILD_TESTS} CACHE BOOL "" FORCE)

# Each core module declares its own target; all dependencies are shared here.
add_subdirectory(core/context_usage)
add_subdirectory(core/event_port)
add_subdirectory(core/provider)
add_subdirectory(core/secrets)
add_subdirectory(core/events)
add_subdirectory(core/sandbox)
add_subdirectory(core/profile_sandbox)
add_subdirectory(core/tools/filesystems)
add_subdirectory(core/tools/runtime)
add_subdirectory(core/sessions)

add_library(homegrowh_harness INTERFACE)
add_library(homegrowh_harness::homegrowh_harness ALIAS homegrowh_harness)
target_compile_features(homegrowh_harness INTERFACE cxx_std_20)
target_include_directories(homegrowh_harness INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(homegrowh_harness INTERFACE
    context_usage event_port provider secrets events sandbox
    filesystems sessions)

# A fresh checkout should not require a separate manual `npm ci` step.
find_program(HH_NPM_EXECUTABLE NAMES npm npm.cmd)
if(NOT HH_NPM_EXECUTABLE)
    message(FATAL_ERROR "npm is required to prepare core/tools")
endif()

set(HH_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core/tools")
set(HH_TOOLS_NPM_STAMP "${CMAKE_CURRENT_BINARY_DIR}/tools-npm-install.stamp")

add_custom_command(
    OUTPUT "${HH_TOOLS_NPM_STAMP}"
    COMMAND "${CMAKE_COMMAND}"
        "-DHH_NPM_EXECUTABLE=${HH_NPM_EXECUTABLE}"
        "-DHH_TOOLS_DIR=${HH_TOOLS_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/npm_ci.cmake"
    COMMAND "${CMAKE_COMMAND}" -E touch "${HH_TOOLS_NPM_STAMP}"
    DEPENDS
        "${HH_TOOLS_DIR}/package.json"
        "${HH_TOOLS_DIR}/package-lock.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/npm_ci.cmake"
    COMMENT "Installing core/tools Node dependencies"
    VERBATIM
)

add_custom_target(tools_node_modules
    DEPENDS "${HH_TOOLS_NPM_STAMP}"
)

add_dependencies(tool_runtime_typescript_tools tools_node_modules)

# Stable runtime artifact surface. Build trees remain configuration-specific
# implementation details; consumers use only files copied into executable/.
add_custom_target(executable
    DEPENDS
        sessions_loop
        process_sandbox
        event_port_emit
        tool_runtime_executable
        edit_file
        tool_runtime_typescript_tools
    COMMAND "${CMAKE_COMMAND}" -E make_directory
        "${HH_EXECUTABLE_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:sessions_loop>"
        "${HH_EXECUTABLE_DIR}/$<TARGET_FILE_NAME:sessions_loop>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:process_sandbox>"
        "${HH_EXECUTABLE_DIR}/$<TARGET_FILE_NAME:process_sandbox>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:event_port_emit>"
        "${HH_EXECUTABLE_DIR}/$<TARGET_FILE_NAME:event_port_emit>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:tool_runtime_executable>"
        "${HH_EXECUTABLE_DIR}/$<TARGET_FILE_NAME:tool_runtime_executable>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "$<TARGET_FILE:edit_file>"
        "${HH_EXECUTABLE_DIR}/$<TARGET_FILE_NAME:edit_file>"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/core/tools/dist"
        "${HH_EXECUTABLE_DIR}/dist"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/core/tools/package.json"
        "${HH_EXECUTABLE_DIR}/package.json"
    USES_TERMINAL
)

add_custom_target(harness_all
    DEPENDS
        context_usage event_port provider secrets events sandbox
        filesystems tool_runtime sessions executable
)
