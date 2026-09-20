#include <fsystem>

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

#include "toolcall_protocol.h"

using json = nlohmann::json;

namespace
{
    constexpr const char* tool_name = "read_file";

    bool read_positive_line(
        const json& item,
        const char* key,
        std::uint64_t& value,
        std::string& error
    )
    {
        if (!item.contains(key))
        {
            error = std::string("Thiếu trường: ") + key;
            return false;
        }

        const json& field = item.at(key);
        if (!field.is_number_unsigned())
        {
            error = std::string(key) + " phải là số nguyên dương";
            return false;
        }

        try
        {
            value = field.get<std::uint64_t>();
        }
        catch (const std::exception&)
        {
            error = std::string(key) + " vượt giới hạn uint64";
            return false;
        }

        if (value == 0)
        {
            error = std::string(key) + " phải >= 1";
            return false;
        }

        return true;
    }

    bool parse_item(
        const json& item,
        fsystem::ReadRequest& out,
        std::string& error
    )
    {
        if (!item.contains("path") || !item.at("path").is_string())
        {
            error = "Thiếu trường chuỗi: path";
            return false;
        }

        out.path = item.at("path").get<std::string>();

        if (!read_positive_line(item, "start_line", out.start_line, error))
            return false;

        if (!read_positive_line(item, "end_line", out.end_line, error))
            return false;

        if (out.end_line < out.start_line)
        {
            error = "end_line phải >= start_line";
            return false;
        }

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
    CLI::App app{"Batch file read tool executable"};
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

    fsystem::ReadRequests requests;
    requests.reserve(invocation.requests.size());

    for (std::size_t index = 0; index < invocation.requests.size(); ++index)
    {
        fsystem::ReadRequest request;
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

    const fsystem::ReadResults read_results = fsystem::read(requests);
    json results = json::array();
    bool all_ok = read_results.size() == requests.size();

    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        if (index >= read_results.size())
        {
            all_ok = false;
            results.push_back({
                {"path", requests[index].path.string()},
                {"start_line", requests[index].start_line},
                {"end_line", requests[index].end_line},
                {"ok", false},
                {"error", 0},
                {"content", ""},
                {"note", "missing_result"}
            });
            continue;
        }

        const fsystem::ReadResult& result = read_results[index];
        const bool item_ok = result.error == 0;
        all_ok = all_ok && item_ok;

        results.push_back({
            {"path", result.path.string()},
            {"start_line", result.start_line},
            {"end_line", result.end_line},
            {"ok", item_ok},
            {"error", result.error},
            {"content", result.content}
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
