#pragma once

#include <provider_types.generated.h>

#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace provider
{
    struct HttpError final : std::runtime_error
    {
        HttpError();

        HttpError(
            long status_code,
            std::string&& status_line,
            std::string&& reason,
            std::string&& body);

        long status_code = 0;
        std::string status_line;
        std::string reason;
        std::string body;
    };

    using EventHandler = void (*)(void*, std::string&&);
    using FinishedHandler = void (*)(void*);

    struct EventSink
    {
        void* context = nullptr;
        EventHandler on_event = nullptr;
        FinishedHandler on_finished = nullptr;
    };

    RequestUsage request(
        Provider provider,
        const std::string& url,
        std::string_view api_key,
        const nlohmann::json& body,
        EventSink sink = {});
}
