#include <tool_runtime>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

namespace
{
    std::string read_text(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }
}

int main()
{
    const std::filesystem::path workspace =
        std::filesystem::temp_directory_path() / "homegrowph_tool_runtime_test";
    const std::filesystem::path outside_workspace =
        std::filesystem::temp_directory_path() /
        "homegrowph_tool_runtime_outside_test";
    std::filesystem::remove_all(workspace);
    std::filesystem::remove_all(outside_workspace);
    std::filesystem::create_directories(workspace);
    std::filesystem::create_directories(outside_workspace);

    const std::filesystem::path outside_file =
        outside_workspace / "outside.txt";
    {
        std::ofstream output(outside_file, std::ios::binary);
        output << "outside\n";
    }

    const nlohmann::json result = tool_runtime::execute(
        {
            {"id", "call_test"},
            {"type", "function"},
            {"function", {
                {"name", "unknown_tool"},
                {"arguments", nlohmann::json::object()}
            }}
        },
        workspace,
        false,
        120000u);

    assert(result.at("role") == "tool");
    assert(result.at("tool_call_id") == "call_test");

    const nlohmann::json content =
        nlohmann::json::parse(result.at("content").get<std::string>());
    assert(content.at("ok") == false);
    assert(content.at("error").at("code") == "unsupported_tool");

    {
        std::ofstream output(workspace / "sample.txt", std::ios::binary);
        output << "alpha\n";
    }

    const nlohmann::json read_result = tool_runtime::execute(
        {
            {"id", "call_read"},
            {"type", "function"},
            {"function", {
                {"name", "read"},
                {"arguments", {
                    {"filePath", "sample.txt"}
                }}
            }}
        },
        workspace,
        true,
        120000u);

    assert(read_result.at("role") == "tool");
    assert(read_result.at("tool_call_id") == "call_read");
    const nlohmann::json read_content =
        nlohmann::json::parse(read_result.at("content").get<std::string>());
    assert(read_content.at("output").get<std::string>().find("alpha")
        != std::string::npos);

    const nlohmann::json missing_result = tool_runtime::execute(
        {
            {"id", "call_missing"},
            {"type", "function"},
            {"function", {
                {"name", "read"},
                {"arguments", {
                    {"filePath", "missing.txt"}
                }}
            }}
        },
        workspace,
        false,
        120000u);

    const nlohmann::json missing_content =
        nlohmann::json::parse(missing_result.at("content").get<std::string>());
    assert(missing_content.at("ok") == false);
    assert(missing_content.at("error").at("code") == "tool_execution_error");
    assert(
        missing_content.at("error")
            .at("source_error")
            .at("code") == "ENOENT");

    const nlohmann::json outside_result = tool_runtime::execute(
        {
            {"id", "call_outside"},
            {"type", "function"},
            {"function", {
                {"name", "read"},
                {"arguments", {
                    {"filePath", outside_file.string()}
                }}
            }}
        },
        workspace,
        false,
        120000u);

    const nlohmann::json outside_content =
        nlohmann::json::parse(outside_result.at("content").get<std::string>());
    if (outside_content.value("ok", true))
    {
        std::cerr << "sandbox unexpectedly read outside workspace: "
                  << outside_content.dump(2) << '\n';
        return 5;
    }
    if (!outside_content.at("error").contains("source_error"))
    {
        std::cerr << "outside-workspace error lost source error: "
                  << outside_content.dump(2) << '\n';
        return 6;
    }

    const nlohmann::json write_result = tool_runtime::execute(
        {
            {"id", "call_write"},
            {"type", "function"},
            {"function", {
                {"name", "write"},
                {"arguments", {
                    {"filePath", "sample.txt"},
                    {"content", "gamma\n"}
                }}
            }}
        },
        workspace,
        true,
        120000u);

    assert(write_result.at("role") == "tool");
    assert(write_result.at("tool_call_id") == "call_write");
    if (read_text(workspace / "sample.txt") != "gamma\n")
    {
        std::cerr << write_result.dump(2) << '\n';
        return 3;
    }

    const nlohmann::json write_reuse_result = tool_runtime::execute(
        {
            {"id", "call_write_reuse"},
            {"type", "function"},
            {"function", {
                {"name", "write"},
                {"arguments", {
                    {"filePath", "sample.txt"},
                    {"content", "delta\n"}
                }}
            }}
        },
        workspace,
        false,
        120000u);

    assert(write_reuse_result.at("role") == "tool");
    assert(write_reuse_result.at("tool_call_id") == "call_write_reuse");
    if (read_text(workspace / "sample.txt") != "delta\n")
    {
        std::cerr << write_reuse_result.dump(2) << '\n';
        return 4;
    }

    std::filesystem::remove_all(workspace);
    std::filesystem::remove_all(outside_workspace);
    return 0;
}
