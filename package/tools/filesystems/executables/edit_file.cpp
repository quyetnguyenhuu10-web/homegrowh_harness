#include <fsystem>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <utility>

#include "toolcall_protocol.h"

using json = nlohmann::json;

namespace
{
    constexpr const char* tool_name = "edit_file";

    const char* to_string(fsystem::EditNote note)
    {
        switch (note)
        {
            case fsystem::EditNote::none:
                return "none";
            case fsystem::EditNote::old_data_not_found:
                return "old_data_not_found";
            case fsystem::EditNote::old_data_appears_more_than_once:
                return "old_data_appears_more_than_once";
            case fsystem::EditNote::file_changed:
                return "file_changed";
            case fsystem::EditNote::old_data_occurrences_overlap:
                return "old_data_occurrences_overlap";
            case fsystem::EditNote::timeout:
                return "timeout";
        }

        return "unknown";
    }

    bool parse_item(
        const json& item,
        fsystem::EditRequest& out,
        std::string& error
    )
    {
        for (const char* key : {"path", "old_content", "new_content"})
        {
            if (!item.contains(key) || !item.at(key).is_string())
            {
                error = std::string("Thiếu trường chuỗi: ") + key;
                return false;
            }
        }

        out.path = item.at("path").get<std::string>();
        out.old_content = item.at("old_content").get<std::string>();
        out.new_content = item.at("new_content").get<std::string>();
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
    CLI::App app{"Batch file edit tool executable"};
    std::filesystem::path toolcall_path;
    bool toolcall_stdin = false;

    app.add_option(
        "--toolcall",
        toolcall_path,
        "Đường dẫn JSON tool call"
    );
    app.add_flag(
        "--toolcall-stdin",
        toolcall_stdin,
        "Đọc JSON tool call từ stdin"
    );
    CLI11_PARSE(app, argc, argv);

    if (toolcall_path.empty() == !toolcall_stdin)
    {
        return fail(
            nullptr,
            "invalid_argument",
            "Chọn đúng một trong --toolcall <path> hoặc --toolcall-stdin"
        );
    }

    std::string input;
    if (toolcall_stdin)
    {
        input.assign(
            std::istreambuf_iterator<char>(std::cin),
            std::istreambuf_iterator<char>()
        );
        if (std::cin.bad())
            return fail(nullptr, "toolcall_read_failed", "Lỗi khi đọc stdin");
    }
    else
    {
        std::ifstream input_stream(toolcall_path, std::ios::binary);
        if (!input_stream)
        {
            return fail(
                nullptr,
                "toolcall_read_failed",
                "Không đọc được file tool call"
            );
        }
        input.assign(
            std::istreambuf_iterator<char>(input_stream),
            std::istreambuf_iterator<char>()
        );
        if (input_stream.bad())
            return fail(nullptr, "toolcall_read_failed", "Lỗi khi đọc file tool call");
    }

    json root;
    try
    {
        root = json::parse(input);
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

    fsystem::EditRequests requests;
    requests.reserve(invocation.requests.size());

    for (std::size_t index = 0; index < invocation.requests.size(); ++index)
    {
        fsystem::EditRequest request;
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

    const fsystem::EditResults edit_results = fsystem::edit(requests);
    json results = json::array();
    bool all_ok = edit_results.size() == requests.size();

    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        if (index >= edit_results.size())
        {
            all_ok = false;
            results.push_back({
                {"path", requests[index].path.string()},
                {"ok", false},
                {"error", 0},
                {"note", "missing_result"},
                {"replace_attempted", false}
            });
            continue;
        }

        const fsystem::EditResult& result = edit_results[index];
        const bool item_ok =
            result.error == 0 && result.note == fsystem::EditNote::none;
        all_ok = all_ok && item_ok;

        results.push_back({
            {"path", result.path.string()},
            {"ok", item_ok},
            {"error", result.error},
            {"note", to_string(result.note)},
            {"replace_attempted", result.replace_attempted}
        });
    }

    std::cout
        << toolcall_protocol::make_response(
            invocation,
            all_ok,
            std::move(results)
        ).dump()
        << '\n';

    // Request hợp lệ đã được xử lý: per-item filesystem errors nằm trong JSON.
    return 0;
}
