#pragma once

#include <stream/stream.h>

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>
#include <provider>

namespace sessions::detail
{
    class ResponseBuilder
    {
    public:
        ResponseBuilder(
            provider::Provider provider,
            const StreamCallback* stream = nullptr) noexcept;

        void append(std::string_view event);
        nlohmann::json finish();

    private:
        struct ToolCall
        {
            std::string id;
            std::string type;
            std::string name;
            std::string arguments;
        };

        bool started_ = false;
        bool has_content_ = false;
        bool has_reasoning_ = false;
        provider::Provider provider_ = provider::Provider::openai;
        const StreamCallback* stream_ = nullptr;
        std::string role_ = "assistant";
        std::string content_;
        std::string reasoning_content_;
        std::map<std::size_t, ToolCall> tool_calls_;
    };
}
