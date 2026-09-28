#include "terminal_event_ui.h"

#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace sessions_loop::util
{
    namespace
    {
        std::string string_value(
            const nlohmann::json& object,
            const char* key)
        {
            const auto found = object.find(key);
            if (found == object.end() || !found->is_string())
                return {};
            return found->get<std::string>();
        }
    }

    TerminalEventUi::TerminalEventUi(provider::Provider selected_provider)
        : provider_(selected_provider)
    {
        registration_.emplace(event_port::port(event_port::Register{
            "",
            {}
        }));
        thread_ = std::thread([this]
        {
            run();
        });
    }

    TerminalEventUi::~TerminalEventUi()
    {
        stop();
    }

    void TerminalEventUi::stop()
    {
        if (stopped_)
            return;
        stopped_ = true;

        if (registration_.has_value())
            event_port::port(event_port::Close{*registration_});

        if (thread_.joinable())
            thread_.join();

        registration_.reset();
        close_summary();
        finish_text();
    }

    void TerminalEventUi::run() noexcept
    {
        for (;;)
        {
            try
            {
                if (!registration_.has_value())
                    return;

                const event_port::EventPtr event = event_port::port(
                    event_port::Read{*registration_});
                if (event != nullptr)
                    handle(*event);
            }
            catch (const std::logic_error&)
            {
                return;
            }
            catch (const std::exception& error)
            {
                finish_text();
                std::cerr << "UI event error: " << error.what() << '\n';
                std::cerr.flush();
            }
            catch (...)
            {
                finish_text();
                std::cerr << "UI event error: unknown exception\n";
                std::cerr.flush();
            }
        }
    }

    void TerminalEventUi::handle(const event_port::Event& event)
    {
        if (event.package == "provider")
        {
            handle_provider(event);
            return;
        }
        if (event.package == "sessions")
            handle_sessions(event);
    }

    void TerminalEventUi::handle_provider(const event_port::Event& event)
    {
        const std::string phase = event.data.value(
            "phase",
            std::string{});

        if (event.type == "started")
        {
            if (phase == "summary" && !summary_open_)
            {
                finish_text();
                std::cout << "[summary]\n";
                std::cout.flush();
                summary_open_ = true;
            }
            return;
        }

        if (event.type == "data")
        {
            project_provider_data(event);
            return;
        }

        if (event.type == "finished")
        {
            if (phase == "summary")
                close_summary();
            return;
        }

        if (event.type == "http_error")
        {
            finish_text();
            const long status_code = event.data.value("status_code", 0L);
            const std::string status_line = string_value(event.data, "status_line");
            const std::string reason = string_value(event.data, "reason");
            const std::string body = string_value(event.data, "body");

            std::cerr << "HTTP error [" << phase << "]\n";
            if (!status_line.empty())
                std::cerr << "status: " << status_line << '\n';
            else
            {
                std::cerr << "status: " << status_code;
                if (!reason.empty())
                    std::cerr << ' ' << reason;
                std::cerr << '\n';
            }
            if (!body.empty())
                std::cerr << "body: " << body << '\n';
            std::cerr.flush();

            if (phase == "summary")
                close_summary();
            return;
        }

        if (event.type == "failed" && phase == "summary")
            close_summary();
    }

    void TerminalEventUi::handle_sessions(const event_port::Event& event)
    {
        if (event.type == "tool_call")
        {
            finish_text();
            std::cout
                << "Đã gọi tool với id: "
                << event.data.value("call_id", std::string{})
                << '\n';
            std::cout.flush();
            return;
        }

        if (event.type == "tool_result")
        {
            finish_text();
            std::cout
                << "Tool result của id: "
                << event.data.value("call_id", std::string{})
                << '\n';
            std::cout.flush();
            return;
        }

        if (event.type == "context_usage")
        {
            finish_text();
            const std::uint64_t used =
                event.data.at("used").get<std::uint64_t>();
            const std::uint64_t limit =
                event.data.at("limit").get<std::uint64_t>();
            const double ratio = limit == 0
                ? 0.0
                : (static_cast<double>(used) * 100.0) /
                    static_cast<double>(limit);

            std::cout
                << "Context usage: "
                << used
                << " / "
                << limit
                << " ("
                << ratio
                << "%)\n";
            std::cout.flush();
            return;
        }

        if (event.type == "secondary_error")
        {
            finish_text();
            std::cerr
                << "Secondary error ["
                << event.data.value("source", std::string("unknown"))
                << "]";

            const std::string operation =
                event.data.value("operation", std::string{});
            if (!operation.empty())
                std::cerr << " operation=" << operation;
            if (event.data.contains("code"))
                std::cerr << " code=" << event.data.at("code");

            const std::string exception =
                event.data.value("exception", std::string{});
            if (!exception.empty())
                std::cerr << " exception=" << exception;

            std::cerr << '\n';
            std::cerr.flush();
        }
    }

    void TerminalEventUi::project_provider_data(
        const event_port::Event& event)
    {
        const auto raw = event.data.find("raw");
        if (raw == event.data.end() || !raw->is_string())
            return;

        const std::string& raw_text = raw->get_ref<const std::string&>();
        if (raw_text.empty() || raw_text == "[DONE]")
            return;

        const nlohmann::json payload = nlohmann::json::parse(
            raw_text,
            nullptr,
            false);
        if (!payload.is_object())
            return;

        const auto choices = payload.find("choices");
        if (choices == payload.end() || !choices->is_array())
            return;

        for (const nlohmann::json& choice : *choices)
        {
            if (!choice.is_object())
                continue;
            const auto delta = choice.find("delta");
            if (delta == choice.end() || !delta->is_object())
                continue;

            if (provider_ != provider::Provider::openai)
            {
                const auto reasoning = delta->find("reasoning_content");
                if (
                    reasoning != delta->end() &&
                    reasoning->is_string() &&
                    !reasoning->get_ref<const std::string&>().empty())
                {
                    begin_text(TextMode::reasoning, "[reasoning] ");
                    std::cout << reasoning->get_ref<const std::string&>();
                    std::cout.flush();
                }
            }

            const auto content = delta->find("content");
            if (
                content != delta->end() &&
                content->is_string() &&
                !content->get_ref<const std::string&>().empty())
            {
                begin_text(TextMode::content, "[content] ");
                std::cout << content->get_ref<const std::string&>();
                std::cout.flush();
            }
        }
    }

    void TerminalEventUi::begin_text(TextMode mode, const char* label)
    {
        if (text_mode_ == mode)
            return;

        finish_text();
        std::cout << label;
        text_mode_ = mode;
    }

    void TerminalEventUi::finish_text()
    {
        if (text_mode_ == TextMode::none)
            return;

        std::cout << '\n';
        std::cout.flush();
        text_mode_ = TextMode::none;
    }

    void TerminalEventUi::close_summary()
    {
        if (!summary_open_)
            return;

        finish_text();
        std::cout << "[/summary]\n";
        std::cout.flush();
        summary_open_ = false;
    }
}
