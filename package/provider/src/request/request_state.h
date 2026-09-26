#pragma once

#include "requests.h"
#include "sse.h"

#include <optional>
#include <utility>

namespace provider
{
    namespace
    {
        class RequestState
        {
        public:
            RequestUsage send(
                const std::string& url,
                std::string_view api_key,
                const nlohmann::json& body,
                Provider provider,
                EventSink sink)
            {
                provider_ = provider;
                sink_ = sink;
                usage_.reset();

                sse_.post(
                    url,
                    api_key,
                    body,
                    this,
                    &RequestState::receive_event);

                if (!usage_.has_value())
                {
                    return UsageState::unavailable;
                }

                return parse_usage(provider_, *usage_);
            }

        private:
            static void receive_event(
                void* context,
                std::string&& event)
            {
                auto* self = static_cast<RequestState*>(context);

                const nlohmann::json payload = nlohmann::json::parse(
                    event,
                    nullptr,
                    false);

                if (!payload.is_discarded())
                {
                    if (auto usage = usage_from_event(
                            self->provider_, payload))
                    {
                        self->usage_ = std::move(*usage);
                    }
                }

                if (self->sink_.on_event != nullptr)
                {
                    self->sink_.on_event(
                        self->sink_.context,
                        std::move(event));
                }
            }

            SSE sse_;
            Provider provider_ = Provider::openai;
            EventSink sink_;
            std::optional<nlohmann::json> usage_;
        };
    }
}
