#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace provider
{
    class SSE
    {
    public:
        using EventHandler = void (*)(void*, std::string&&);

        void post(
            const std::string& url,
            const std::string& api_key,
            const nlohmann::json& body,
            void* context,
            EventHandler on_event,
            std::uint32_t* error);
    };
}
