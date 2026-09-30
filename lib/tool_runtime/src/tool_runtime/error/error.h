#pragma once

#include <exception>
#include <string_view>
#include <system_error>

#include <tool_runtime/tool_runtime.h>

namespace tool_runtime::detail
{
    nlohmann::json text_payload(std::string_view text);
    Error make_error(
        std::string_view operation,
        std::string_view type,
        std::string_view message,
        nlohmann::json&& details = nlohmann::json::object());
    Error make_system_error(
        std::string_view operation,
        std::string_view api,
        const std::error_code& code,
        nlohmann::json&& details = nlohmann::json::object());
    Error exception_error(
        std::string_view operation,
        const std::exception& exception,
        nlohmann::json&& details = nlohmann::json::object());
    Error current_exception_error(std::string_view operation);
    Error dependency_error(
        std::string_view operation,
        std::string_view message,
        std::vector<Error>&& causes,
        nlohmann::json&& details = nlohmann::json::object());
    void append_error(std::optional<Error>& target, Error&& error);

    Result<nlohmann::json> parse_json(
        std::string_view input,
        std::string_view operation,
        nlohmann::json&& details = nlohmann::json::object());
    Error adapt_error(
        const nlohmann::json& input,
        std::string_view source,
        std::string_view operation);
}
