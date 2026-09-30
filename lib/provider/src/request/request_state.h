#pragma once
#include "requests.h"
#include "sse.h"

namespace provider
{
    class RequestState
    {
    public:
        RequestState(Provider selected_provider, EventSink sink);
        Result<RequestUsage> send(
            const std::string& url,
            std::string_view api_key,
            const nlohmann::json& body);

    private:
        static Result<void> receive_event(void* context, std::string&& event);

        SSE sse_;
        Provider provider_;
        EventSink sink_;
        std::optional<nlohmann::json> usage_;
    };
}
