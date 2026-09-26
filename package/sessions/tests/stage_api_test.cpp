#include <sessions>

#include <type_traits>

int main()
{
    static_assert(!std::is_copy_constructible_v<sessions::Session>);
    static_assert(std::is_move_constructible_v<sessions::Session>);
    static_assert(!std::is_copy_constructible_v<sessions::RequestStage>);
    static_assert(!std::is_copy_constructible_v<sessions::ResponseStage>);
    static_assert(!std::is_copy_constructible_v<sessions::ToolStage>);

    [[maybe_unused]] auto* register_api = &sessions::register_session;
    [[maybe_unused]] auto* close_api = &sessions::close_session;
    [[maybe_unused]] auto* declare_request_api = &sessions::declare_request;
    [[maybe_unused]] auto* run_request_api = &sessions::run_request;
    [[maybe_unused]] auto* declare_response_api = &sessions::declare_response;
    [[maybe_unused]] auto* run_response_api = &sessions::run_response;
    [[maybe_unused]] auto* declare_tool_api = &sessions::declare_tool;
    [[maybe_unused]] auto* run_tool_api = &sessions::run_tool;

    sessions::SessionConfig config;
    config.tool_result_timeout_ms = -1;
    config.session_timeout_ms = -1;
    config.event_log = [](const event_port::Event& event)
    {
        (void)event.sequence;
    };

    return 0;
}
