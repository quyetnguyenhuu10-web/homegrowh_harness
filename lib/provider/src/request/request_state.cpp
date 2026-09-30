#include "request_state.h"
#include "error/capture.h"

namespace provider
{
    RequestState::RequestState(Provider selected_provider, EventSink sink)
        : provider_(selected_provider), sink_(sink)
    {
    }

    Result<RequestUsage> RequestState::send(
        const std::string& url,
        std::string_view api_key,
        const nlohmann::json& body)
    {
        auto sent = sse_.post(url, api_key, body, this, &RequestState::receive_event);
        if (!sent)
        {
            return Result<RequestUsage>::failure(std::move(*sent.error));
        }
        if (!usage_.has_value())
        {
            return Result<RequestUsage>::success(
                RequestUsage{UsageState::unavailable});
        }
        return parse_usage(provider_, *usage_);
    }

    Result<void> RequestState::receive_event(void* context, std::string&& event)
    {
        auto* self = static_cast<RequestState*>(context);
        try
        {
            if (event != "[DONE]")
            {
                const nlohmann::json payload = nlohmann::json::parse(event);
                if (!payload.is_object())
                {
                    return Result<void>::failure(error_detail::make_error(
                        "receive_event", "protocol_error",
                        "Provider event must be an object", {payload}));
                }
                const auto native_error = payload.find("error");
                if (native_error != payload.end() && !native_error->is_null())
                {
                    auto normalized = deserialize_error(*native_error);
                    if (normalized)
                    {
                        return Result<void>::failure(std::move(*normalized.value));
                    }
                    std::string description = "Provider returned an error event";
                    if (native_error->is_object())
                    {
                        const auto message = native_error->find("message");
                        if (message != native_error->end() && message->is_string())
                        {
                            description = message->get<std::string>();
                        }
                    }
                    return Result<void>::failure(error_detail::make_error(
                        "request", "provider_error", description,
                        {{{"event", payload}}}));
                }
                auto usage = usage_from_event(self->provider_, payload);
                if (!usage)
                {
                    return Result<void>::failure(std::move(*usage.error));
                }
                if (usage.value->has_value())
                {
                    self->usage_ = std::move(**usage.value);
                }
            }
            if (self->sink_.on_event != nullptr)
            {
                try
                {
                    auto dispatched = self->sink_.on_event(
                        self->sink_.context, std::move(event));
                    if (!dispatched)
                    {
                        return dispatched;
                    }
                }
                catch (...)
                {
                    return Result<void>::failure(error_detail::capture_exception(
                        std::current_exception(), "dispatch_event",
                        {{{"api", "EventSink::on_event"}, {"event", event}}}));
                }
            }
            return Result<void>::success();
        }
        catch (...)
        {
            return Result<void>::failure(error_detail::capture_exception(
                std::current_exception(), "receive_event", {{{"event", event}}}));
        }
    }
}
