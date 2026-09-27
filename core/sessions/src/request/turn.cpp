#include "turn.h"
#include <response/response.h>

#include <request/request.h>

#include <event_port>

#include <atomic>
#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace sessions::detail
{
    namespace
    {
        std::atomic<std::uint64_t> next_stream_id{0};

        event_port::References stream_references(const std::string& stream_id)
        {
            event_port::References references;
            std::string type = "stream_id";
            std::string value = stream_id;
            references.emplace_back(std::move(type), std::move(value));
            return references;
        }

        struct PortSinkContext
        {
            const std::string* stream_id = nullptr;
            const char* phase = nullptr;
            bool* finished = nullptr;
        };

        void publish_port_event(
            const PortSinkContext& context,
            event_port::Level level,
            std::string&& type,
            nlohmann::json&& data)
        {
            event_port::port(event_port::Emit{
                "provider",
                level,
                std::move(type),
                stream_references(*context.stream_id),
                std::move(data)
            });
        }

        void receive_provider_event(void* raw_context, std::string&& raw)
        {
            auto* context = static_cast<PortSinkContext*>(raw_context);

            nlohmann::json data = {
                {"phase", context->phase},
                {"raw", std::move(raw)}
            };

            publish_port_event(
                *context,
                event_port::Level::info,
                "data",
                std::move(data));
        }

        void finish_provider_stream(void* raw_context)
        {
            auto* context = static_cast<PortSinkContext*>(raw_context);

            publish_port_event(
                *context,
                event_port::Level::info,
                "finished",
                nlohmann::json{{"phase", context->phase}});

            if (context->finished != nullptr)
            {
                *context->finished = true;
            }
        }

        provider::EventSink provider_sink(PortSinkContext* context) noexcept
        {
            return provider::EventSink{
                context,
                &receive_provider_event,
                &finish_provider_stream
            };
        }

        const char* session_failure_phase(SessionState state) noexcept
        {
            switch (state)
            {
                case SessionState::request: return "request";
                case SessionState::response: return "response";
                case SessionState::tool: return "tool";
                case SessionState::finished: return "finished";
                case SessionState::closed: return "closed";
            }
            return "unknown";
        }
    }

    void emit_session_failure(
        const SessionFailure& failure,
        const EventLogCallback& event_log) noexcept
    {
        try
        {
            nlohmann::json raw = nullptr;

            if (failure.exception != nullptr)
            {
                try
                {
                    std::rethrow_exception(failure.exception);
                }
                catch (const std::exception& error)
                {
                    raw = error.what();
                }
                catch (...)
                {
                }
            }

            event_port::EventPtr event = event_port::port(event_port::Emit{
                "sessions",
                event_port::Level::error,
                "failed",
                {},
                nlohmann::json{
                    {"phase", session_failure_phase(failure.state)},
                    {"raw", std::move(raw)}
                }
            });

            if (event_log && event != nullptr)
            {
                try
                {
                    event_log(*event);
                }
                catch (...)
                {
                }
            }
        }
        catch (...)
        {
        }
    }

    TurnResult run_turn(
        const std::string& api_key_signature,
        const std::string& endpoint,
        const std::string& model_id,
        provider::Provider selected_provider,
        std::string_view compaction_prompt,
        const nlohmann::json& session_current,
        const nlohmann::json& tool_definitions,
        bool compact,
        const nlohmann::json& history,
        const StreamCallback& stream,
        const EventLogCallback& event_log)
    {
        ResponseBuilder builder(selected_provider, &stream);
        const bool has_summary = compact && !history.empty();
        const StreamCallback summary_stream =
            [&](StreamType type, std::string_view value)
            {
                if (!stream)
                    return;

                if (type == StreamType::reasoning)
                {
                    stream(StreamType::summary_reasoning, value);
                    return;
                }
                if (type == StreamType::content)
                {
                    stream(StreamType::summary_content, value);
                }
            };
        ResponseBuilder summary_builder(
            selected_provider,
            &summary_stream);

        const std::string stream_id =
            "provider-" + std::to_string(
                next_stream_id.fetch_add(1, std::memory_order_relaxed));

        event_port::Registration registration = event_port::port(
            event_port::Register{
                "provider",
                stream_references(stream_id)
            });

        bool summary_finished = !has_summary;
        PortSinkContext request_context{
            &stream_id,
            "request",
            nullptr
        };
        PortSinkContext summary_context{
            &stream_id,
            "summary",
            &summary_finished
        };

        if (has_summary && stream)
        {
            stream(StreamType::summary_start, {});
        }

        bool summary_closed = !has_summary;
        const auto close_summary = [&]
        {
            if (summary_closed)
                return;

            summary_closed = true;
            if (stream)
                stream(StreamType::summary_end, {});
        };

        std::optional<provider::CompactionResult> request_result;
        std::exception_ptr request_error;
        std::exception_ptr stream_error;
        std::exception_ptr event_log_error;
        std::thread request_thread([&]
        {
            try
            {
                request_result = sessions::request(
                    api_key_signature,
                    selected_provider,
                    endpoint,
                    model_id,
                    compaction_prompt,
                    session_current,
                    tool_definitions,
                    compact,
                    history,
                    provider_sink(&request_context),
                    has_summary
                        ? provider_sink(&summary_context)
                        : provider::EventSink{},
                    nullptr);
            }
            catch (const provider::HttpError& error)
            {
                request_error = std::current_exception();

                const PortSinkContext& context = summary_finished
                    ? request_context
                    : summary_context;

                publish_port_event(
                    context,
                    event_port::Level::error,
                    "http_error",
                    nlohmann::json{
                        {"phase", context.phase},
                        {"status_code", error.status_code},
                        {"status_line", error.status_line},
                        {"reason", error.reason},
                        {"body", error.body}
                    });
            }
            catch (const std::exception& error)
            {
                request_error = std::current_exception();

                const PortSinkContext& context = summary_finished
                    ? request_context
                    : summary_context;

                publish_port_event(
                    context,
                    event_port::Level::error,
                    "failed",
                    nlohmann::json{
                        {"phase", context.phase},
                        {"raw", error.what()}
                    });
            }
            catch (...)
            {
                request_error = std::current_exception();

                const PortSinkContext& context = summary_finished
                    ? request_context
                    : summary_context;

                publish_port_event(
                    context,
                    event_port::Level::error,
                    "failed",
                    nlohmann::json{
                        {"phase", context.phase},
                        {"raw", nullptr}
                    });
            }
        });

        bool finished = false;
        while (!finished)
        {
            const event_port::EventPtr event = event_port::port(
                event_port::Read{registration});

            if (event_log && event_log_error == nullptr)
            {
                try
                {
                    event_log(*event);
                }
                catch (...)
                {
                    event_log_error = std::current_exception();
                }
            }

            const std::string phase = event->data.value(
                "phase",
                std::string{});

            if (event->type == "http_error")
            {
                if (stream)
                {
                    const std::string serialized = event->data.dump();
                    stream(StreamType::http_error, serialized);
                }

                close_summary();
                finished = true;
                continue;
            }

            if (event->type == "failed")
            {
                close_summary();
                finished = true;
                continue;
            }

            if (event->type == "finished")
            {
                if (phase == "summary")
                {
                    close_summary();
                    continue;
                }

                finished = true;
                continue;
            }

            if (event->type != "data" || stream_error != nullptr)
            {
                continue;
            }

            try
            {
                const std::string& raw =
                    event->data.at("raw").get_ref<const std::string&>();

                if (phase == "summary")
                {
                    summary_builder.append(raw);
                }
                else
                {
                    builder.append(raw);
                }
            }
            catch (...)
            {
                stream_error = std::current_exception();
            }
        }

        request_thread.join();
        close_summary();

        if (request_error != nullptr)
        {
            std::rethrow_exception(request_error);
        }

        if (event_log_error != nullptr)
        {
            std::rethrow_exception(event_log_error);
        }

        if (stream_error != nullptr)
        {
            std::rethrow_exception(stream_error);
        }
        if (!request_result.has_value())
        {
            throw std::runtime_error(
                "provider request completed without a result");
        }

        return TurnResult{
            std::move(*request_result),
            builder.finish()
        };
    }
}
