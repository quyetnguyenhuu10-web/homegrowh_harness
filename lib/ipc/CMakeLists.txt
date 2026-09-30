if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(ipc STATIC)
add_library(ipc::ipc ALIAS ipc)

hh_require_nlohmann_json()
hh_require_threads()

target_compile_features(ipc PUBLIC cxx_std_20)
target_include_directories(ipc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(ipc
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE Threads::Threads
)
target_sources(ipc PRIVATE
    src/error.cpp
    src/error.h
    src/framing.cpp
    src/framing.h
)

if(WIN32)
    target_sources(ipc PRIVATE
        src/windows/ipc.cpp
        src/windows/io.cpp
        src/windows/io.h
        src/windows/resource.h
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(ipc PRIVATE
        src/linux/ipc.cpp
        src/linux/io.cpp
        src/linux/io.h
        src/linux/resource.h
    )
else()
    message(FATAL_ERROR "ipc supports only Windows and Linux")
endif()

option(IPC_BUILD_TESTS "Build ipc tests" OFF)

if(IPC_BUILD_TESTS)
    enable_testing()

    add_executable(ipc_test
        test/ipc_test.cpp
    )
    target_link_libraries(ipc_test PRIVATE ipc::ipc)

    add_test(NAME ipc_test COMMAND ipc_test)

    add_executable(ipc_error_test test/error_test.cpp)
    target_link_libraries(ipc_error_test PRIVATE ipc::ipc)
    add_test(NAME ipc_error_test COMMAND ipc_error_test)

    add_executable(ipc_protocol_test test/protocol_test.cpp)
    target_include_directories(ipc_protocol_test PRIVATE src)
    target_link_libraries(ipc_protocol_test PRIVATE ipc::ipc)
    add_test(NAME ipc_protocol_test COMMAND ipc_protocol_test)

    set_tests_properties(ipc_test ipc_error_test ipc_protocol_test
        PROPERTIES TIMEOUT 30)
endif()
