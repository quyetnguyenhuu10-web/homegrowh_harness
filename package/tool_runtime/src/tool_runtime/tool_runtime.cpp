#include "runtime/runtime.h"

#include <iostream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

int main()
{
    const std::string raw{
        std::istreambuf_iterator<char>(std::cin),
        std::istreambuf_iterator<char>()};

    const nlohmann::json input =
        nlohmann::json::parse(raw, nullptr, false);
    if (input.is_discarded())
    {
        const tool_runtime::execution_result result =
            tool_runtime::detail::execute_invalid_json(raw);
        std::cout << nlohmann::json{
            {"tool_call", result.tool_call},
            {"result", result.result}
        }.dump();
        return 0;
    }

    const tool_runtime::execution_result result =
        tool_runtime::detail::execute_tool(input);
    std::cout << nlohmann::json{
        {"tool_call", result.tool_call},
        {"result", result.result}
    }.dump();
    return 0;
}
