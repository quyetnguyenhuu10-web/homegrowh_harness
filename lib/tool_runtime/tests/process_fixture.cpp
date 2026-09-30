#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

int main(int argc, char** argv)
{
    const std::string mode = argc > 1 ? argv[1] : "echo";
    if (mode == "no-input")
        return 0;
    if (mode == "arguments")
    {
        nlohmann::json arguments = nlohmann::json::array();
        for (int index = 2; index < argc; ++index)
            arguments.push_back(argv[index]);
        std::cout << arguments.dump();
        return std::cout ? 0 : 1;
    }
    const std::string input{std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()};
    if (std::cin.bad())
        return 1;
    if (mode == "streams")
    {
        std::cout << input;
        std::cerr << input;
    }
    else if (const char* response = std::getenv("TOOL_RUNTIME_FIXTURE_RESPONSE"); response != nullptr)
        std::cout << response;
    else
        std::cout << input;
    if (!std::cout || !std::cerr)
        return 1;
    const char* code = std::getenv("TOOL_RUNTIME_FIXTURE_EXIT_CODE");
    return code == nullptr ? 0 : std::atoi(code);
}
