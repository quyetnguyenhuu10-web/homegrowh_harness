if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(profile_sandbox_api STATIC
    src/api/process.cpp
    src/api/process.h
    src/api/registry.cpp
    src/api/registry.h
    src/api/filesystem.cpp
    src/api/filesystem.h
)
add_library(profile_sandbox::api ALIAS profile_sandbox_api)

target_compile_features(profile_sandbox_api PUBLIC cxx_std_20)
target_link_libraries(profile_sandbox_api PRIVATE Threads::Threads)

if(WIN32)
    target_sources(profile_sandbox_api PRIVATE
        src/windows/process.cpp
        src/windows/process.h
        src/windows/appcontainer.cpp
        src/windows/appcontainer.h
        src/windows/job.cpp
        src/windows/job.h
        src/windows/raii.h
        src/windows/filesystem.cpp
        src/windows/filesystem.h
        src/windows/acl.cpp
        src/windows/acl.h
        src/windows/error.h
        src/windows/identity.cpp
        src/windows/identity.h
        src/windows/state.cpp
        src/windows/state.h
    )
    target_link_libraries(profile_sandbox_api PRIVATE
        Advapi32
        Bcrypt
        Userenv
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(profile_sandbox_api PRIVATE
        src/linux/process.cpp
        src/linux/process.h
        src/linux/landlock.cpp
        src/linux/landlock.h
        src/linux/filesystem.cpp
        src/linux/filesystem.h
        src/linux/identity.cpp
        src/linux/identity.h
        src/linux/state.cpp
        src/linux/state.h
    )
else()
    message(FATAL_ERROR "profile_sandbox does not support this operating system")
endif()

add_executable(process_sandbox
    src/executable/process_sandbox.cpp
)
target_compile_features(process_sandbox PRIVATE cxx_std_20)
target_link_libraries(process_sandbox PRIVATE
    profile_sandbox::api
    nlohmann_json::nlohmann_json
)

option(PROFILE_SANDBOX_BUILD_TESTS "Build profile_sandbox tests" OFF)

if(PROFILE_SANDBOX_BUILD_TESTS)
    add_executable(profile_sandbox_registry_test
        tests/registry_test.cpp
    )
    target_link_libraries(profile_sandbox_registry_test PRIVATE profile_sandbox::api)

    add_executable(profile_sandbox_process_test
        tests/process_test.cpp
    )
    target_link_libraries(profile_sandbox_process_test PRIVATE profile_sandbox::api)
endif()
