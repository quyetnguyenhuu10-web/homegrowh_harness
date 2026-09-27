#include <response/response.h>

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }
}

int main()
{
    std::vector<std::pair<sessions::StreamType, std::string>> streamed;
    const sessions::StreamCallback stream =
        [&](sessions::StreamType type, std::string_view delta)
        {
            streamed.emplace_back(type, delta);
        };

    sessions::detail::ResponseBuilder builder(
        provider::Provider::deepseek,
        &stream);

    builder.append(R"({"choices":[{"index":0,"delta":{"role":"assistant","reasoning_content":"think ","tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read","arguments":"{\"filePath\":\""}}]}}]})");
    builder.append(R"({"choices":[{"index":0,"delta":{"reasoning_content":"more","content":"done","tool_calls":[{"index":0,"function":{"arguments":"a.cpp\"}"}}]},"finish_reason":"tool_calls"}]})");
    builder.append(R"({"usage":{"prompt_tokens":10,"completion_tokens":2,"total_tokens":12},"choices":[]})");
    builder.append("[DONE]");

    const nlohmann::json message = builder.finish();
    require(message.at("role") == "assistant", "assistant role mismatch");
    require(message.at("content") == "done", "content accumulation mismatch");
    require(
        message.at("reasoning_content") == "think more",
        "reasoning accumulation mismatch");

    const nlohmann::json& calls = message.at("tool_calls");
    require(calls.size() == 1, "tool call count mismatch");
    require(calls.at(0).at("id") == "call_1", "tool call id mismatch");
    require(
        calls.at(0).at("function").at("name") == "read",
        "tool call name mismatch");
    require(
        calls.at(0).at("function").at("arguments") ==
            R"({"filePath":"a.cpp"})",
        "tool call argument accumulation mismatch");

    require(streamed.size() == 3, "stream callback count mismatch");
    require(
        streamed.at(0).first == sessions::StreamType::reasoning &&
        streamed.at(0).second == "think ",
        "first reasoning stream mismatch");
    require(
        streamed.at(1).first == sessions::StreamType::content &&
        streamed.at(1).second == "done",
        "content stream mismatch");
    require(
        streamed.at(2).first == sessions::StreamType::reasoning &&
        streamed.at(2).second == "more",
        "second reasoning stream mismatch");

    sessions::detail::ResponseBuilder openai(provider::Provider::openai);
    openai.append(R"({"choices":[{"index":0,"delta":{"role":"assistant","reasoning_content":"must not surface","content":"visible"},"finish_reason":"stop"}]})");
    const nlohmann::json openai_message = openai.finish();
    require(
        !openai_message.contains("reasoning_content"),
        "OpenAI Chat Completions reasoning must not be surfaced");
    require(openai_message.at("content") == "visible", "OpenAI content mismatch");

    sessions::detail::ResponseBuilder bonsai(provider::Provider::bonsai);
    bonsai.append(R"({"choices":[{"index":0,"delta":{"role":"assistant","reasoning_content":"local think","content":"local answer"},"finish_reason":"stop"}]})");
    const nlohmann::json bonsai_message = bonsai.finish();
    require(
        bonsai_message.at("reasoning_content") == "local think",
        "Bonsai reasoning mismatch");

    std::cout << "sessions loop response tests passed\n";
    return 0;
}
