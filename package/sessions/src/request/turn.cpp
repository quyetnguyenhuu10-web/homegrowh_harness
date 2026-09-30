#include "turn.h"
#include <request/request.h>
#include <tool/tool_stream_parser.h>
#include <error/error.h>

#include <event_port>
#include <error/event_port.h>

#include <atomic>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
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
            ToolStreamParser* tool_parser = nullptr;
            std::uint64_t delta_sequence = 0;
        };

        event_port::EventPtr publish_port_event(
            const PortSinkContext& context,
            event_port::Level level,
            std::string&& type,
            nlohmann::json&& data)
        {
            return sessions::detail::checked_port(event_port::Emit{
                "provider",
                level,
                std::move(type),
                stream_references(*context.stream_id),
                std::move(data)
            });
        }

        provider::Result<void> receive_provider_event(void* raw_context, std::string&& raw)
        {
            try
            {
                auto* context = static_cast<PortSinkContext*>(raw_context);

                nlohmann::json data = {
                    {"phase", context->phase},
                    {"delta_sequence", context->delta_sequence++},
                    {"raw", std::move(raw)}
                };

                const event_port::EventPtr emitted = publish_port_event(
                    *context,
                    event_port::Level::info,
                    "data",
                    std::move(data));

                if (context->tool_parser != nullptr)
                {
                    context->tool_parser->append(
                        emitted->data.at("raw").get_ref<const std::string&>());
                }
                return provider::Result<void>::success();
            }
            catch (const std::exception& exception)
            {
                return provider::Result<void>::failure(convert_error<provider::Error>(
                    exception_error("receive_provider_event", exception)));
            }
            catch (...)
            {
                return provider::Result<void>::failure(
                    provider::Error{"sessions", "receive_provider_event", "unknown_exception", "", {}, {}});
            }
        }

        provider::Result<void> finish_provider_stream(void* raw_context)
        {
            try
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
                return provider::Result<void>::success();
            }
            catch (const std::exception& exception)
            {
                return provider::Result<void>::failure(convert_error<provider::Error>(
                    exception_error("finish_provider_stream", exception)));
            }
            catch (...)
            {
                return provider::Result<void>::failure(
                    provider::Error{"sessions", "finish_provider_stream", "unknown_exception", "", {}, {}});
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

    void emit_session_failure(const SessionFailure& failure) noexcept
    {
        try
        {
            const char* phase = session_failure_phase(failure.state);
            nlohmann::json payload = {
                {"phase", phase},
                {"error", exception_error(phase, failure.exception)}
            };
            sessions::detail::checked_port(event_port::Emit{
                "sessions",
                event_port::Level::error,
                "failed",
                {},
                std::move(payload)
            });
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
        const nlohmann::json& history)
    {
        const bool has_summary = compact && !history.empty();
        ToolStreamParser tool_parser;

        const std::string stream_id =
            "provider-" + std::to_string(
                next_stream_id.fetch_add(1, std::memory_order_relaxed));

        bool summary_finished = !has_summary;
        PortSinkContext request_context{
            &stream_id,
            "request",
            nullptr,
            &tool_parser
        };
        PortSinkContext summary_context{
            &stream_id,
            "summary",
            &summary_finished,
            nullptr
        };

        if (has_summary)
        {
            publish_port_event(
                summary_context,
                event_port::Level::info,
                "started",
                nlohmann::json{{"phase", "summary"}});
        }

        provider::CompactionResult request_result = [&]
        {
            try
            {
                return sessions::request(
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
            catch (const ErrorException&)
            {
                // The Session boundary publishes this structured failure once.
                throw;
            }
            catch (const std::exception& error)
            {
                const PortSinkContext& context = summary_finished
                    ? request_context
                    : summary_context;

                publish_port_event(
                    context,
                    event_port::Level::error,
                    "failed",
                    nlohmann::json{
                        {"phase", context.phase},
                        {"error", exception_error("request", error)}
                    });
                throw;
            }
            catch (...)
            {
                const PortSinkContext& context = summary_finished
                    ? request_context
                    : summary_context;

                publish_port_event(
                    context,
                    event_port::Level::error,
                    "failed",
                    nlohmann::json{
                        {"phase", context.phase},
                        {"error", exception_error("request", std::current_exception())}
                    });
                throw;
            }
        }();

        return TurnResult{
            std::move(request_result),
            tool_parser.finish()
        };
    }
}
