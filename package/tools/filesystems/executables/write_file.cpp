#include <fsystem>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "toolcall_protocol.h"

using json = nlohmann::json;

namespace
{
    constexpr const char* tool_name = "write_file";

    bool parse_item(
        const json& item,
        fsystem::WriteRequest& out,
        std::string& error
    )
    {
        for (const char* key : {"path", "new_content"})
        {
            if (!item.contains(key) || !item.at(key).is_string())
            {
                error = std::string("Thiếu trường chuỗi: ") + key;
                return false;
            }
        }

        std::string path = item.at("path").get<std::string>();
        std::string new_content = item.at("new_content").get<std::string>();

        out.path = std::move(path);
        out.new_content = std::move(new_content);
        return true;
    }

    int fail(
        const json& call_id,
        const std::string& code,
        const std::string& message,
        int exit_code = 2
    )
    {
        std::cout
            << toolcall_protocol::make_error_response(
                tool_name,
                call_id,
                code,
                message
            ).dump()
            << '\n';
        return exit_code;
    }
}

int main(int argc, char* argv[])
{
    CLI::App app{"Batch file write tool executable"};
    std::filesystem::path toolcall_path;

    app.add_option(
        "--toolcall",
        toolcall_path,
        "Đường dẫn JSON tool call"
    );
    CLI11_PARSE(app, argc, argv);

    if (toolcall_path.empty())
        return fail(nullptr, "missing_argument", "Thiếu --toolcall <path>");

    const fsystem::ReadResult input = fsystem::read(toolcall_path);
    if (input.error != 0)
    {
        return fail(
            nullptr,
            "toolcall_read_failed",
            "Không đọc được file tool call, mã lỗi: " +
                std::to_string(input.error)
        );
    }

    json root;
    try
    {
        root = json::parse(input.content);
    }
    catch (const std::exception& exception)
    {
        return fail(nullptr, "invalid_json", exception.what());
    }

    toolcall_protocol::Invocation invocation;
    std::string parse_error;

    if (!toolcall_protocol::parse_invocation(
            root,
            tool_name,
            invocation,
            parse_error
        ))
    {
        const json call_id =
            root.is_object() && root.contains("call_id")
                ? root.at("call_id")
                : json(nullptr);
        return fail(call_id, "invalid_toolcall", parse_error);
    }

    if (invocation.requests.empty())
        return fail(invocation.call_id, "empty_requests", "Không có request nào");

    fsystem::WriteRequests requests;
    requests.reserve(invocation.requests.size());

    for (std::size_t index = 0; index < invocation.requests.size(); ++index)
    {
        fsystem::WriteRequest request;
        std::string error;

        if (!parse_item(invocation.requests[index], request, error))
        {
            return fail(
                invocation.call_id,
                "invalid_request",
                "requests[" + std::to_string(index) + "]: " + error
            );
        }

        requests.push_back(std::move(request));
    }

    const fsystem::WriteResults write_results = fsystem::write(requests);
    json results = json::array();
    bool all_ok = write_results.size() == requests.size();

    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        if (index >= write_results.size())
        {
            all_ok = false;
            results.push_back({
                {"path", requests[index].path.string()},
                {"ok", false},
                {"error", 0},
                {"note", "missing_result"}
            });
            continue;
        }

        const fsystem::WriteResult& result = write_results[index];
        const bool item_ok = result.error == 0;
        all_ok = all_ok && item_ok;

        results.push_back({
            {"path", result.path.string()},
            {"ok", item_ok},
            {"error", result.error}
        });
    }

    std::cout
        << toolcall_protocol::make_response(
            invocation,
            all_ok,
            std::move(results)
        ).dump()
        << '\n';

    return 0;
}
