#pragma once

#include <event_port>
#include <provider>

#include <optional>
#include <thread>

namespace sessions_loop::util
{
    class TerminalEventUi final
    {
    public:
        explicit TerminalEventUi(provider::Provider selected_provider);

        TerminalEventUi(const TerminalEventUi&) = delete;
        TerminalEventUi& operator=(const TerminalEventUi&) = delete;

        ~TerminalEventUi();

        void stop();

    private:
        enum class TextMode
        {
            none,
            reasoning,
            content,
        };

        void run() noexcept;
        void handle(const event_port::Event& event);
        void handle_provider(const event_port::Event& event);
        void handle_sessions(const event_port::Event& event);
        void project_provider_data(const event_port::Event& event);
        void begin_text(TextMode mode, const char* label);
        void finish_text();
        void close_summary();

        provider::Provider provider_;
        std::optional<event_port::Registration> registration_;
        std::thread thread_;
        TextMode text_mode_ = TextMode::none;
        bool summary_open_ = false;
        bool stopped_ = false;
    };
}
