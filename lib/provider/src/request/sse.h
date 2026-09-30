#pragma once

#include <provider>

namespace provider
{
    class SSE
    {
    public:
        using EventHandler = Result<void> (*)(void*, std::string&&);

        Result<void> post(
            const std::string& url,
            std::string_view api_key,
            const nlohmann::json& body,
            void* context,
            EventHandler on_event);
    };
}
