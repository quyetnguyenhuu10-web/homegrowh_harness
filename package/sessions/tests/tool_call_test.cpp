#include <tool/tool_call.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    nlohmann::json parse_content(const nlohmann::json& message)
    {
        return nlohmann::json::parse(message.at("content").get<std::string>());
    }
}

int main()
{
    try
    {
    const nlohmann::json definitions = nlohmann::json::parse(R"json(
        [
          {
            "type": "function",
            "function": {
              "name": "read",
              "description": "read",
              "parameters": {
                "type": "object",
                "additionalProperties": false,
                "properties": {
                  "filePath": {"type": "string"}
                },
                "required": ["filePath"]
              }
            }
          }
        ]
    )json");

    std::vector<std::string> stream_events;
    const sessions::StreamCallback stream =
        [&](sessions::StreamType type, std::string_view value)
        {
            if (type == sessions::StreamType::tool_call)
                stream_events.push_back("call:" + std::string(value));
            else if (type == sessions::StreamType::tool_result)
                stream_events.push_back("result:" + std::string(value));
        };

    sessions::detail::ToolCallHandler handler(
        definitions,
        std::filesystem::path("__hh_missing_workspace_for_tool_call_test__"),
        false,
        &stream);

    const nlohmann::json malformed = {
        {"type", "not-function"},
        {"function", {
            {"name", "read"},
            {"arguments", "{broken"}
        }}
    };

    sessions::detail::HandledToolCall malformed_result =
        handler.handle(malformed);

    require(
        malformed_result.tool_call.at("type") == "function",
        "tool call type was not canonicalized");
    require(
        !malformed_result.tool_call.at("id").get<std::string>().empty(),
        "missing tool id was not synthesized");
    require(
        malformed_result.tool_call.at("function").at("name") == "read",
        "tool name was not canonicalized from schema");
    const nlohmann::json malformed_payload =
        parse_content(malformed_result.result_message);
    require(
        malformed_payload.at("tool") == definitions.at(0),
        "tool result must contain the exact matching schema");
    require(
        malformed_payload.at("results").size() == 1,
        "malformed tool result must contain one result item");
    require(
        !malformed_payload.at("results").at(0).at("ok").get<bool>(),
        "malformed tool result must be an error result item");
    require(
        stream_events.size() == 2,
        "malformed tool call must emit call and result signals");
    require(
        stream_events.at(0).rfind("call:", 0) == 0 &&
        stream_events.at(1).rfind("result:", 0) == 0,
        "tool stream signal order mismatch");
    require(
        stream_events.at(0).substr(5) == stream_events.at(1).substr(7),
        "tool call and result signals must use the same id");

    stream_events.clear();

    const nlohmann::json split = {
        {"type", "function"},
        {"function", {
            {"name", "read"},
            {"arguments", {{"filePath", "b.txt"}}}
        }}
    };

    sessions::detail::PreparedToolCall split_first = handler.prepare(split);
    sessions::detail::PreparedToolCall split_second = handler.prepare(split);

    require(
        stream_events.empty(),
        "tool preparation must not emit tool events");
    require(
        split_first.tool_call.at("id") == split_second.tool_call.at("id"),
        "redeclaring a tool before execution must keep the synthesized id stable");

    sessions::detail::HandledToolCall split_result =
        handler.execute(std::move(split_second));
    require(
        stream_events.size() == 2,
        "tool execution must emit call and result signals");
    require(
        split_result.tool_call.at("id") == split_first.tool_call.at("id"),
        "executed tool id must match the declared tool id");

    stream_events.clear();

    const nlohmann::json valid = {
        {"id", "call_read"},
        {"type", "function"},
        {"function", {
            {"name", "read"},
            {"arguments", {{"filePath", "a.txt"}}}
        }}
    };

    sessions::detail::HandledToolCall runtime_error_result = handler.handle(valid);

    const nlohmann::json runtime_error_payload =
        parse_content(runtime_error_result.result_message);
    require(
        runtime_error_payload.at("tool") == definitions.at(0),
        "runtime error result must contain the exact matching schema");
    require(
        runtime_error_payload.at("results").size() == 1,
        "runtime error result must contain one result item");
    require(
        runtime_error_payload.at("results").at(0).at("ok") == false,
        "runtime error result must report ok=false");
    require(
        runtime_error_payload.at("results").at(0).contains("error"),
        "runtime error result must contain error details");
    require(
        stream_events == std::vector<std::string>{
            "call:call_read",
            "result:call_read"},
        "runtime tool stream signals must contain only the call id");

    std::cout << "sessions tool call tests passed\n";
    return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
