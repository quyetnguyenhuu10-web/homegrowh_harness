if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(tool_runtime LANGUAGES CXX)
    set(TOOL_RUNTIME_TESTS_DEFAULT ON)
else()
    set(TOOL_RUNTIME_TESTS_DEFAULT ${HH_BUILD_TESTS})
endif()

if(NOT TARGET nlohmann_json::nlohmann_json)
    find_package(nlohmann_json CONFIG REQUIRED)
endif()
find_package(Threads REQUIRED)
option(TOOL_RUNTIME_BUILD_TESTS "Build isolated tool_runtime tests" ${TOOL_RUNTIME_TESTS_DEFAULT})
if(TARGET edit_file)
    set(TOOL_RUNTIME_PACKAGE_DEFAULT ON)
else()
    set(TOOL_RUNTIME_PACKAGE_DEFAULT OFF)
endif()
option(TOOL_RUNTIME_PACKAGE_WORKERS "Build and package external tool workers" ${TOOL_RUNTIME_PACKAGE_DEFAULT})

add_library(tool_runtime_core STATIC
    src/tool_runtime/error/error.cpp
    src/tool_runtime/error/error.h
    src/tool_runtime/platform/environment.cpp
    src/tool_runtime/platform/platform.cpp
    src/tool_runtime/platform/platform.h
    src/tool_runtime/process/process.cpp
    src/tool_runtime/process/process.h
    src/tool_runtime/runtime/execution.cpp
    src/tool_runtime/runtime/response.cpp
    src/tool_runtime/runtime/response.h
    src/tool_runtime/runtime/runtime.cpp
    src/tool_runtime/runtime/runtime.h
    include/tool_runtime/tool_runtime.h
)
if(WIN32)
    target_sources(tool_runtime_core PRIVATE src/tool_runtime/process/windows/process.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(tool_runtime_core PRIVATE src/tool_runtime/process/linux/process.cpp)
else()
    message(FATAL_ERROR "Unsupported operating system")
endif()
target_compile_features(tool_runtime_core PUBLIC cxx_std_20)
target_include_directories(tool_runtime_core PUBLIC include PRIVATE src)
target_link_libraries(tool_runtime_core PUBLIC nlohmann_json::nlohmann_json PRIVATE Threads::Threads)
if(MSVC)
    target_compile_options(tool_runtime_core PRIVATE /utf-8)
endif()

add_executable(tool_runtime src/tool_runtime/tool_runtime.cpp)
target_include_directories(tool_runtime PRIVATE src/tool_runtime)
target_link_libraries(tool_runtime PRIVATE tool_runtime_core)
if(DEFINED HH_EXECUTABLE_DIR)
    set_target_properties(tool_runtime PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${HH_EXECUTABLE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${HH_EXECUTABLE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${HH_EXECUTABLE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${HH_EXECUTABLE_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${HH_EXECUTABLE_DIR}"
    )
endif()

if(TOOL_RUNTIME_PACKAGE_WORKERS)
    if(NOT TARGET edit_file)
        message(FATAL_ERROR "Worker packaging requires the edit_file target")
    endif()
    set(TOOLS_PACKAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tools")
    find_program(NPM_EXECUTABLE NAMES npm npm.cmd REQUIRED)
    add_custom_target(tool_runtime_typescript_tools
        COMMAND "${NPM_EXECUTABLE}" run build
        WORKING_DIRECTORY "${TOOLS_PACKAGE_DIR}"
        COMMENT "Building TypeScript tool worker"
        VERBATIM
    )
    add_dependencies(tool_runtime tool_runtime_typescript_tools edit_file)
    add_custom_command(TARGET tool_runtime POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory "${TOOLS_PACKAGE_DIR}/dist" "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TOOLS_PACKAGE_DIR}/package.json" "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools/package.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TOOLS_PACKAGE_DIR}/src/tool_definitions.json" "$<TARGET_FILE_DIR:tool_runtime>/tool_runtime_tools/tool_definitions.json"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:edit_file>" "$<TARGET_FILE_DIR:tool_runtime>/$<TARGET_FILE_NAME:edit_file>"
        VERBATIM
    )
else()
    add_custom_target(tool_runtime_typescript_tools)
endif()

if(TOOL_RUNTIME_BUILD_TESTS)
    enable_testing()
    add_executable(tool_runtime_process_fixture tests/process_fixture.cpp)
    target_compile_features(tool_runtime_process_fixture PRIVATE cxx_std_20)
    target_link_libraries(tool_runtime_process_fixture PRIVATE nlohmann_json::nlohmann_json)
    add_executable(tool_runtime_tests tests/tool_runtime_tests.cpp)
    target_include_directories(tool_runtime_tests PRIVATE src/tool_runtime)
    target_link_libraries(tool_runtime_tests PRIVATE tool_runtime_core)
    add_test(NAME tool_runtime_errors
        COMMAND tool_runtime_tests "$<TARGET_FILE:tool_runtime_process_fixture>")
    set_tests_properties(tool_runtime_errors PROPERTIES TIMEOUT 60)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME tool_runtime_protocol
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/protocol_tests.py"
                "$<TARGET_FILE:tool_runtime>" "$<TARGET_FILE:tool_runtime_process_fixture>")
        set_tests_properties(tool_runtime_protocol PROPERTIES TIMEOUT 60)
    endif()
endif()

