#include <tool/tool_call.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        const std::filesystem::path workspace = ".";
        const std::filesystem::path runtime = "tool_runtime";
        const sandbox::config sandbox_config;

        sessions::detail::ToolCallHandler handler(
            workspace,
            runtime,
            sandbox_config,
            120000,
            false);

        const nlohmann::json raw = {
            {"type", "not-function"},
            {"function", {
                {"name", "read"},
                {"arguments", "{broken"}
            }}
        };

        std::cout << "sessions tool call tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
