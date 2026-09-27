if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Configure from the repository root")
endif()

add_library(event_port
    src/event_port/event.cpp
    src/event_port/event_port.h
    src/event_port/port.cpp
    src/event_port/registration.cpp
    src/event_port/detail/clock.cpp
    src/event_port/detail/clock.h
    src/event_port/detail/event_access.h
    src/event_port/detail/registration_state.cpp
    src/event_port/detail/registration_state.h
    src/event_port/detail/registry.cpp
    src/event_port/detail/registry.h
    src/event_port/detail/sequence.cpp
    src/event_port/detail/sequence.h
    src/event_port/detail/validate.cpp
    src/event_port/detail/validate.h
    src/ipc/channel.cpp
    src/ipc/channel.h
    src/ipc/ingress.cpp
    src/ipc/ingress.h
    src/ipc/platform.h
)

if(WIN32)
    target_sources(event_port PRIVATE
        src/windows/ipc.cpp
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(event_port PRIVATE
        src/linux/ipc.cpp
    )
else()
    message(FATAL_ERROR "event_port IPC does not support this operating system")
endif()

add_library(event_port::event_port ALIAS event_port)

target_compile_features(event_port PUBLIC cxx_std_20)

target_include_directories(event_port
    PUBLIC
        include
        src
)

target_link_libraries(event_port
    PUBLIC
        nlohmann_json::nlohmann_json
    PRIVATE
        Threads::Threads
)

add_executable(event_port_emit
    src/executable/emit.cpp
)
target_compile_features(event_port_emit PRIVATE cxx_std_20)
target_link_libraries(event_port_emit PRIVATE event_port)

option(EVENT_PORT_BUILD_TESTS "Build event_port tests" OFF)

if(EVENT_PORT_BUILD_TESTS)
    enable_testing()

    add_executable(event_port_test
        tests/event_port_test.cpp
    )
    target_link_libraries(event_port_test PRIVATE event_port::event_port)

    add_test(NAME event_port_test COMMAND event_port_test)
endif()
