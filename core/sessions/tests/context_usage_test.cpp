#include <context_usage>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    template <typename Exception, typename Callable>
    void require_throws(Callable&& callable, const char* message)
    {
        try
        {
            callable();
        }
        catch (const Exception&)
        {
            return;
        }
        throw std::runtime_error(message);
    }

    std::uint64_t utf8_characters(const std::string& text)
    {
        std::uint64_t count = 0;
        for (const unsigned char value : text)
        {
            if ((value & 0xC0u) != 0x80u)
                ++count;
        }
        return count;
    }

    std::uint64_t serialized_estimate(const nlohmann::json& value)
    {
        return utf8_characters(value.dump()) / 4;
    }
}

int main()
{
    using nlohmann::json;

    const json body = json::array({
        {
            {"role", "user"},
            {"content", "12345678"},
        },
        {
            {"role", "assistant"},
            {"reasoning_content", "abcd"},
            {"content", "wxyz"},
        },
    });

    require(
        context_usage::estimate(body) == serialized_estimate(body),
        "message estimate must use the full serialized model-visible message array");

    const json unicode = json::array({
        {
            {"role", "user"},
            {"content", "áéíó"},
        },
    });
    require(
        context_usage::estimate(unicode) == serialized_estimate(unicode),
        "UTF-8 serialized message estimate mismatch");

    const json structured = json::array({
        {
            {"role", "user"},
            {"content", json::array({"ab", "cd"})},
        },
    });
    require(
        context_usage::estimate(structured) == serialized_estimate(structured),
        "structured content estimate mismatch");

    const json assistant_without_tool_call = json::array({
        {
            {"role", "assistant"},
            {"content", nullptr},
            {"reasoning_content", "thinking"}
        }
    });
    const json assistant_with_tool_call = json::array({
        {
            {"role", "assistant"},
            {"content", nullptr},
            {"reasoning_content", "thinking"},
            {"tool_calls", json::array({
                {
                    {"id", "call_123"},
                    {"type", "function"},
                    {"function", {
                        {"name", "read"},
                        {"arguments", "{\"filePath\":\"D:\\\\tools\\\\a.cpp\"}"}
                    }}
                }
            })}
        }
    });
    require(
        context_usage::estimate(assistant_with_tool_call)
            > context_usage::estimate(assistant_without_tool_call),
        "assistant tool_calls must contribute to usage estimate");

    const json tool_result_without_id = json::array({
        {
            {"role", "tool"},
            {"content", "result"}
        }
    });
    const json tool_result_with_id = json::array({
        {
            {"role", "tool"},
            {"tool_call_id", "call_123"},
            {"content", "result"}
        }
    });
    require(
        context_usage::estimate(tool_result_with_id)
            > context_usage::estimate(tool_result_without_id),
        "tool_call_id must contribute to tool-result usage estimate");

    const json current = {
        {"messages", body},
        {"tools", json::array({
            {
                {"type", "function"},
                {"function", {
                    {"name", "x"},
                    {"description", "12345678"},
                    {"parameters", json::object()}
                }}
            }
        })}
    };
    require(
        context_usage::estimate(current) > context_usage::estimate(body),
        "tool definitions must contribute to context estimate");
    require(
        context_usage::estimate(current) == serialized_estimate(current),
        "context estimate must serialize the full messages/tools context");

    const json current_with_transport_fields = {
        {"messages", body},
        {"tools", current.at("tools")},
        {"model", "bonsai"},
        {"stream", true},
        {"stream_options", {{"include_usage", true}}}
    };
    require(
        context_usage::estimate(current_with_transport_fields)
            == context_usage::estimate(current),
        "transport/routing fields must not contribute to context estimate");

    require(
        context_usage::openai(100, 40) == 140,
        "OpenAI context usage formula changed");
    require(
        context_usage::deepseek(60, 40, 50) == 150,
        "DeepSeek context usage formula changed");
    require(
        context_usage::bonsai(60, 40, 50) == 150,
        "Bonsai context usage formula changed");

    require_throws<std::overflow_error>(
        []
        {
            (void)context_usage::openai(
                std::numeric_limits<std::uint64_t>::max(),
                1);
        },
        "exact context usage overflow behavior changed");

    std::cout << "context usage tests passed\n";
    return 0;
}
