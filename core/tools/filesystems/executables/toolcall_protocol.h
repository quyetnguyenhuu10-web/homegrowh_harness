#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace toolcall_protocol
{
    using json = nlohmann::json;

    /*
     * Provider-agnostic normalized tool call:
     * {
     *   "version": 1,
     *   "tool": "read_file",
     *   "call_id": "call_...",
     *   "arguments": { "requests": [ ... ] }
     * }
     *
     * Provider-specific tool-call parsing belongs in the TypeScript router.
     * Every filesystem executable can reuse this envelope and only parse its
     * own request items.
     */
    struct Invocation
    {
        std::uint32_t version = 1;
        std::string tool;
        json call_id = nullptr;
        json requests = json::array();
        bool legacy_input = false;
    };

    inline bool append_requests(
        const json& source,
        json& requests,
        std::string& error
    )
    {
        if (source.is_array())
        {
            for (const json& item : source)
            {
                if (!item.is_object())
                {
                    error = "requests phải là mảng object";
                    return false;
                }

                requests.push_back(item);
            }

            return true;
        }

        if (source.is_object())
        {
            requests.push_back(source);
            return true;
        }

        error = "requests phải là object hoặc mảng object";
        return false;
    }

    inline bool parse_invocation(
        const json& root,
        const std::string& expected_tool,
        Invocation& invocation,
        std::string& error
    )
    {
        invocation.tool = expected_tool;

        const bool envelope =
            root.is_object() &&
            (
                root.contains("tool") ||
                root.contains("arguments") ||
                root.contains("version") ||
                root.contains("call_id")
            );

        if (!envelope)
        {
            invocation.legacy_input = true;
            return append_requests(root, invocation.requests, error);
        }

        if (!root.is_object())
        {
            error = "tool call envelope phải là object";
            return false;
        }

        if (root.contains("version"))
        {
            if (!root.at("version").is_number_unsigned())
            {
                error = "version phải là số nguyên không âm";
                return false;
            }

            invocation.version = root.at("version").get<std::uint32_t>();
            if (invocation.version != 1)
            {
                error = "version tool call chưa được hỗ trợ";
                return false;
            }
        }

        if (!root.contains("tool") || !root.at("tool").is_string())
        {
            error = "Thiếu trường chuỗi: tool";
            return false;
        }

        invocation.tool = root.at("tool").get<std::string>();
        if (invocation.tool != expected_tool)
        {
            error = "Sai tool: expected " + expected_tool +
                ", received " + invocation.tool;
            return false;
        }

        if (root.contains("call_id"))
        {
            const json& call_id = root.at("call_id");
            if (!call_id.is_null() && !call_id.is_string())
            {
                error = "call_id phải là string hoặc null";
                return false;
            }

            invocation.call_id = call_id;
        }

        if (!root.contains("arguments"))
        {
            error = "Thiếu trường: arguments";
            return false;
        }

        const json& arguments = root.at("arguments");

        if (arguments.is_object() && arguments.contains("requests"))
        {
            return append_requests(
                arguments.at("requests"),
                invocation.requests,
                error
            );
        }

        // Chấp nhận arguments là request đơn / mảng request để caller thủ công
        // không bắt buộc phải bọc thêm { "requests": ... }.
        return append_requests(arguments, invocation.requests, error);
    }

    inline json make_response(
        const Invocation& invocation,
        bool ok,
        json results
    )
    {
        return json{
            {"version", 1},
            {"tool", invocation.tool},
            {"call_id", invocation.call_id},
            {"ok", ok},
            {"results", std::move(results)}
        };
    }

    inline json make_error_response(
        const std::string& tool,
        const json& call_id,
        const std::string& code,
        const std::string& message
    )
    {
        return json{
            {"version", 1},
            {"tool", tool},
            {"call_id", call_id},
            {"ok", false},
            {"results", json::array()},
            {
                "error",
                {
                    {"code", code},
                    {"message", message}
                }
            }
        };
    }
}
