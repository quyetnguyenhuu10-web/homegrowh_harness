#include "runtime/runtime.h"

#include "error/error.h"

#include <array>
#include <cerrno>
#include <iostream>
#include <string>

int main()
{
    tool_runtime::execution_result result;
    try
    {
        std::string raw;
        std::array<char, 8192> buffer{};
        while (std::cin)
        {
            errno = 0;
            std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const int code = errno;
            raw.append(buffer.data(), static_cast<std::size_t>(std::cin.gcount()));
            if (std::cin.bad() || (std::cin.fail() && !std::cin.eof()))
            {
                auto details = nlohmann::json{{"api", "std::cin.read"}, {"rdstate", std::cin.rdstate()}};
                auto error = code != 0
                    ? tool_runtime::detail::make_system_error("read_input", "std::cin.read",
                        {code, std::generic_category()}, std::move(details))
                    : tool_runtime::detail::make_error("read_input", "io_error", "Input stream failed", std::move(details));
                result = tool_runtime::detail::execute_error(nlohmann::json::object(), std::move(error));
                break;
            }
        }
        if (result.result.is_null())
            result = tool_runtime::detail::execute_invalid_json(raw);
    }
    catch (...)
    {
        result = tool_runtime::detail::execute_error(
            nlohmann::json::object(), tool_runtime::detail::current_exception_error("execute_input"));
    }

    try
    {
        const auto output = nlohmann::json{{"tool_call", result.tool_call}, {"result", result.result}}.dump();
        errno = 0;
        std::cout.write(output.data(), static_cast<std::streamsize>(output.size()));
        std::cout.flush();
        if (!std::cout)
        {
            const int code = errno;
            const auto error = code != 0
                ? tool_runtime::detail::make_system_error("write_output", "std::cout.write",
                    {code, std::generic_category()}, {{"rdstate", std::cout.rdstate()}})
                : tool_runtime::detail::make_error("write_output", "io_error", "Output stream failed",
                    {{"api", "std::cout.write"}, {"rdstate", std::cout.rdstate()}});
            std::cerr << nlohmann::json{{"error", error}}.dump();
            return 1;
        }
    }
    catch (...)
    {
        std::cerr << nlohmann::json{{"error", tool_runtime::detail::current_exception_error("write_output")}}.dump();
        return 1;
    }
    return 0;
}

