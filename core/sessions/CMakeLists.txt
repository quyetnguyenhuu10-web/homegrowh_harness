foreach(required_target IN ITEMS
    context_usage event_port provider secrets events sandbox)
    if(NOT TARGET "${required_target}")
        message(FATAL_ERROR
            "sessions requires ${required_target}; configure from the root CMakeLists.txt")
    endif()
endforeach()

add_library(sessions
    src/context/context.cpp
    src/context/context.h
    src/convert_history/openai.cpp
    src/convert_history/deepseek.cpp
    src/convert_history/bonsai.cpp
    src/loop/full_loop.cpp
    src/loop/loop.h
    src/request/request.cpp
    src/request/request.h
    src/request/turn.cpp
    src/request/turn.h
    src/response/reasoning.cpp
    src/response/reasoning.h
    src/response/response.cpp
    src/response/response.h
    src/session/credential_owner.cpp
    src/session/credential_owner.h
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
    src/stream/stream.h
    src/tool/tool_call.cpp
    src/tool/tool_call.h
)
add_library(sessions::sessions ALIAS sessions)

target_compile_features(sessions PUBLIC cxx_std_20)
target_include_directories(sessions PUBLIC include src)
target_link_libraries(sessions
    PUBLIC nlohmann_json::nlohmann_json provider event_port
    PRIVATE context_usage secrets events sandbox
)

add_executable(sessions_loop excutable/loop/main.cpp)
target_link_libraries(sessions_loop PRIVATE sessions)

option(SESSIONS_BUILD_TESTS "Build sessions tests" OFF)
if(SESSIONS_BUILD_TESTS)
    add_executable(sessions_context_usage_test tests/context_usage_test.cpp)
    target_link_libraries(sessions_context_usage_test PRIVATE
        sessions context_usage)

    add_executable(sessions_request_test tests/request_test.cpp)
    target_link_libraries(sessions_request_test PRIVATE
        sessions context_usage)

    add_executable(sessions_loop_response_test
        tests/loop_response_test.cpp)
    target_link_libraries(sessions_loop_response_test PRIVATE sessions)

    add_executable(sessions_tool_call_test tests/tool_call_test.cpp)
    target_link_libraries(sessions_tool_call_test PRIVATE sessions)

    add_executable(sessions_stage_api_test tests/stage_api_test.cpp)
    target_link_libraries(sessions_stage_api_test PRIVATE sessions)
endif()
