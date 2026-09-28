if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(ipc STATIC)
add_library(ipc::ipc ALIAS ipc)

target_compile_features(ipc PUBLIC cxx_std_20)
target_include_directories(ipc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_sources(ipc PRIVATE
        src/windows/ipc.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(ipc PRIVATE
        src/linux/ipc.cpp
    )
else()
    message(FATAL_ERROR "ipc supports only Windows and Linux")
endif()

option(IPC_BUILD_TESTS "Build ipc tests" OFF)

if(IPC_BUILD_TESTS)
    enable_testing()

    add_executable(ipc_test
        tests/ipc_test.cpp
    )
    target_link_libraries(ipc_test PRIVATE ipc::ipc)

    add_test(NAME ipc_test COMMAND ipc_test)
endif()
