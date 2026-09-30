foreach(required_target IN ITEMS
    context_usage event_port provider secrets sandbox ipc)
    if(NOT TARGET "${required_target}")
        message(FATAL_ERROR
            "sessions requires ${required_target}; configure from the root CMakeLists.txt")
    endif()
endforeach()

add_library(sessions
    src/context/context.cpp
    src/context/context.h
    src/loop/full_loop.cpp
    src/loop/loop.h
    src/request/request.cpp
    src/request/request.h
    src/request/turn.cpp
    src/request/turn.h
    src/session/credential_owner.cpp
    src/session/credential_owner.h
    src/error/error.h
    src/error/event_port.h
    src/session/request.cpp
    src/session/request.h
    src/session/response.cpp
    src/session/response.h
    src/session/session_state.cpp
    src/session/session_state.h
    src/session/session_failure.h
    src/session/session_timeout.cpp
    src/session/session_timeout.h
    src/session/session.cpp
    src/session/session.h
    src/session/session_internal.h
    src/session/tool.cpp
    src/session/tool.h
    src/tool/tool_call.cpp
    src/tool/tool_call.h
    src/tool/tool_stream_parser.cpp
    src/tool/tool_stream_parser.h
)
add_library(sessions::sessions ALIAS sessions)

target_compile_features(sessions PUBLIC cxx_std_20)
target_include_directories(sessions
    PUBLIC include
    PRIVATE src
)
target_link_libraries(sessions
    PUBLIC nlohmann_json::nlohmann_json provider event_port sandbox secrets
    PRIVATE context_usage
)

add_executable(sessions_loop
    excutable/loop/main.cpp
    excutable/loop/util/terminal_event_ui.cpp
    excutable/loop/util/terminal_event_ui.h
)
target_link_libraries(sessions_loop PRIVATE sessions)
target_include_directories(sessions_loop PRIVATE src)
add_dependencies(sessions_loop tool_runtime)

add_executable(session_runtime
    excutable/runtime/main.cpp
    excutable/runtime/command.cpp
    excutable/runtime/command.h
    excutable/runtime/config_parser.cpp
    excutable/runtime/config_parser.h
    excutable/runtime/event_forwarder.cpp
    excutable/runtime/event_forwarder.h
    excutable/runtime/ipc_failure.h
    excutable/runtime/runtime.cpp
    excutable/runtime/runtime_event.cpp
    excutable/runtime/runtime.h
    excutable/runtime/runtime_request.cpp
    excutable/runtime/runtime_response.cpp
    excutable/runtime/runtime_session.cpp
    excutable/runtime/runtime_tool.cpp
    excutable/runtime/wire_json.cpp
    excutable/runtime/wire_json.h
)
target_link_libraries(session_runtime PRIVATE sessions ipc)
target_include_directories(session_runtime PRIVATE src)
add_dependencies(session_runtime tool_runtime)

option(SESSIONS_BUILD_TESTS "Build sessions tests" OFF)
if(SESSIONS_BUILD_TESTS)
    add_executable(sessions_error_boundary_test
        tests/error_boundary_test.cpp
        excutable/runtime/command.cpp
        excutable/runtime/config_parser.cpp
        excutable/runtime/runtime.cpp
        excutable/runtime/runtime_event.cpp
        excutable/runtime/runtime_request.cpp
        excutable/runtime/runtime_response.cpp
        excutable/runtime/runtime_session.cpp
        excutable/runtime/runtime_tool.cpp
        excutable/runtime/wire_json.cpp)
    target_include_directories(sessions_error_boundary_test PRIVATE src excutable/runtime)
    target_link_libraries(sessions_error_boundary_test PRIVATE sessions ipc)

    add_executable(sessions_turn_failure_test
        tests/turn_failure_test.cpp
        src/request/turn.cpp
        src/tool/tool_stream_parser.cpp)
    target_include_directories(sessions_turn_failure_test PRIVATE include src)
    target_link_libraries(sessions_turn_failure_test PRIVATE provider event_port sandbox secrets)

    add_executable(sessions_secret_error_test
        tests/secret_error_test.cpp
        excutable/runtime/wire_json.cpp)
    target_include_directories(sessions_secret_error_test PRIVATE src excutable/runtime)
    target_link_libraries(sessions_secret_error_test PRIVATE sessions ipc)

    add_executable(sessions_context_usage_test tests/context_usage_test.cpp)
    target_link_libraries(sessions_context_usage_test PRIVATE
        sessions context_usage)

    add_executable(sessions_request_test tests/request_test.cpp)
    target_include_directories(sessions_request_test PRIVATE src)
    target_link_libraries(sessions_request_test PRIVATE
        sessions context_usage)

    add_executable(sessions_tool_stream_parser_test
        tests/tool_stream_parser_test.cpp)
    target_include_directories(sessions_tool_stream_parser_test PRIVATE src)
    target_link_libraries(sessions_tool_stream_parser_test PRIVATE sessions)

    add_executable(sessions_tool_call_test tests/tool_call_test.cpp)
    target_include_directories(sessions_tool_call_test PRIVATE src)
    target_link_libraries(sessions_tool_call_test PRIVATE sessions)

    add_executable(sessions_stage_api_test tests/stage_api_test.cpp)
    target_link_libraries(sessions_stage_api_test PRIVATE sessions)
endif()
