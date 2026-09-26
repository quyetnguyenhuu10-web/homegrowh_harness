#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace provider
{
    class SSE
    {
    public:
        using EventHandler = void (*)(void*, std::string&&);

        void post(
            const std::string& url,
            std::string_view api_key,
            const nlohmann::json& body,
            void* context,
            EventHandler on_event);
    };
}
