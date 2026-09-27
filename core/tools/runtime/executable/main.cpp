#include <tool_runtime>

#include <cstdint>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

int main()
{
    try
    {
        std::ostringstream input_stream;
        input_stream << std::cin.rdbuf();
        if (std::cin.bad())
            throw std::runtime_error("tool_runtime failed to read stdin");
        const std::string input = input_stream.str();
        const nlohmann::json request = nlohmann::json::parse(input);

        if (!request.is_object()
            || !request.contains("tool_call"))
        {
            throw std::invalid_argument(
                "tool_runtime request requires tool_call");
        }

        const std::uint32_t timeout_ms = request.value("timeout_ms", 120000u);
        std::vector<std::string> read_files;
        if (const auto files = request.find("read_files"); files != request.end())
        {
            if (!files->is_array())
                throw std::invalid_argument("tool_runtime read_files must be an array");
            read_files.reserve(files->size());
            for (const nlohmann::json& item : *files)
            {
                if (!item.is_string())
                    throw std::invalid_argument("tool_runtime read_files item must be a string");
                read_files.push_back(item.get<std::string>());
            }
        }

        const nlohmann::json result = tool_runtime::execute(
            request.at("tool_call"),
            read_files,
            timeout_ms);

        std::cout << nlohmann::json{
            {"result_message", result},
            {"read_files", read_files},
        }.dump();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what();
        return 2;
    }
}
