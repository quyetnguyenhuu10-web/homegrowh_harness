#include <tool/tool_stream_parser.h>

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>

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
    sessions::detail::ToolStreamParser parser;

    parser.append(R"({"choices":[{"index":0,"delta":{"role":"assistant","reasoning_content":"ignored","content":"ignored","tool_calls":[{"index":0,"id":"call_1","type":"function","function":{"name":"read","arguments":"{\"filePath\":\""}}]}}]})");
    parser.append(R"({"choices":[{"index":0,"delta":{"reasoning_content":"still ignored","content":"still ignored","tool_calls":[{"index":0,"function":{"arguments":"a.cpp\"}"}}]}}]})");
    parser.append(R"({"usage":{"prompt_tokens":10,"completion_tokens":2,"total_tokens":12},"choices":[]})");
    parser.append("[DONE]");

    const nlohmann::json calls = parser.finish();
    require(calls.size() == 1, "tool call count mismatch");
    require(calls.at(0).at("id") == "call_1", "tool call id mismatch");
    require(
        calls.at(0).at("function").at("name") == "read",
        "tool call name mismatch");
    require(
        calls.at(0).at("function").at("arguments") ==
            R"({"filePath":"a.cpp"})",
        "tool call argument accumulation mismatch");

    sessions::detail::ToolStreamParser malformed;
    malformed.append(R"({"choices":[{"delta":{"tool_calls":{"function":"bad"}}}]})");
    malformed.append(R"({"choices":[{"delta":{"tool_calls":[{"id":"call_missing_index","function":{"name":7}}]}}]})");

    const nlohmann::json malformed_calls = malformed.finish();
    require(
        malformed_calls.size() == 2,
        "malformed tool calls must be forwarded instead of rejected");

    std::cout << "sessions tool stream parser tests passed\n";
    return 0;
}
