if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(sandbox STATIC
    src/sandbox/run.cpp
    src/sandbox/run.h
)
add_library(sandbox::sandbox ALIAS sandbox)

target_compile_features(sandbox PUBLIC cxx_std_20)
target_include_directories(sandbox
    PUBLIC include
    PRIVATE src
)

if(WIN32)
    target_sources(sandbox PRIVATE
        src/windows/launch.cpp
        src/windows/process/job.cpp
        src/windows/process/job.h
        src/windows/process/raii.h
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(sandbox PRIVATE
        src/linux/launch.cpp
        src/linux/process/process_group.cpp
        src/linux/process/process_group.h
    )
else()
    message(FATAL_ERROR "sandbox does not have a process backend for this operating system")
endif()

option(SANDBOX_BUILD_TESTS "Build sandbox tests" OFF)

if(SANDBOX_BUILD_TESTS)
    enable_testing()

    add_executable(sandbox_run_test tests/run_test.cpp)
    target_link_libraries(sandbox_run_test PRIVATE sandbox::sandbox)

    add_test(NAME sandbox_run_test COMMAND sandbox_run_test)
endif()
