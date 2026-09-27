#include "../api/process.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace
{
    std::filesystem::path path_from_utf8(const std::string& value)
    {
        const auto* begin = reinterpret_cast<const char8_t*>(value.data());
        return std::filesystem::path(std::u8string(begin, begin + value.size()));
    }

    std::string path_to_utf8(const std::filesystem::path& value)
    {
        const std::u8string text = value.u8string();
        return std::string(
            reinterpret_cast<const char*>(text.data()),
            text.size());
    }

    sandbox::permission parse_permission(const std::string& value)
    {
        if (value == "read_only")
            return sandbox::permission::read_only;
        if (value == "read_write")
            return sandbox::permission::read_write;
        throw std::invalid_argument("process_sandbox: unknown filesystem permission");
    }

    sandbox::network_access parse_network(const nlohmann::json& value)
    {
        if (value.is_null() || value == "none")
            return sandbox::network_access::none;
        if (value == "internet_client")
            return sandbox::network_access::internet_client;
        throw std::invalid_argument("process_sandbox: unknown network access");
    }

    nlohmann::json error_json(const std::error_code& error)
    {
        if (!error)
            return nullptr;
        return {
            {"code", error.value()},
            {"message", error.message()}
        };
    }
}

int main()
{
    try
    {
        std::ostringstream input_stream;
        input_stream << std::cin.rdbuf();
        if (std::cin.bad())
            throw std::runtime_error("process_sandbox failed to read stdin");
        const std::string raw = input_stream.str();
        const nlohmann::json input = nlohmann::json::parse(raw);
        if (!input.is_object())
            throw std::invalid_argument("process_sandbox request must be an object");

        sandbox::process_request request;
        request.executable = path_from_utf8(input.at("executable").get<std::string>());
        request.working_directory = path_from_utf8(
            input.at("working_directory").get<std::string>());
        request.stdin_data = input.value("stdin", std::string{});
        request.timeout = std::chrono::milliseconds(
            input.value("timeout_ms", static_cast<std::uint32_t>(120000)));
        request.refresh = input.value("refresh", false);
        request.network = input.contains("network")
            ? parse_network(input.at("network"))
            : sandbox::network_access::none;

        if (const auto args = input.find("arguments"); args != input.end())
        {
            if (!args->is_array())
                throw std::invalid_argument("process_sandbox arguments must be an array");
            for (const nlohmann::json& item : *args)
                request.arguments.push_back(item.get<std::string>());
        }

        if (const auto filesystem = input.find("filesystem"); filesystem != input.end())
        {
            if (!filesystem->is_array())
                throw std::invalid_argument("process_sandbox filesystem must be an array");
            for (const nlohmann::json& item : *filesystem)
            {
                request.filesystem.push_back({
                    path_from_utf8(item.at("path").get<std::string>()),
                    parse_permission(item.at("access").get<std::string>())
                });
            }
        }

        const sandbox::process_result result = sandbox::process(request);
        nlohmann::json path_errors = nlohmann::json::array();
        for (const sandbox::registry_path_error& item : result.state.registry.path_errors)
        {
            path_errors.push_back({
                {"path", path_to_utf8(item.path)},
                {"error", error_json(item.error)}
            });
        }

        std::cout << nlohmann::json{
            {"started", result.state.started},
            {"timed_out", result.state.timed_out},
            {"terminated", result.state.terminated},
            {"exit_code", result.state.exit_code},
            {"os_error_before_termination", error_json(result.state.os_error_before_termination)},
            {"final_error", error_json(result.state.final_error)},
            {"registry_final_error", error_json(result.state.registry.final_error)},
            {"path_errors", std::move(path_errors)},
            {"stdout", result.stdout_text},
            {"stderr", result.stderr_text}
        }.dump();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what();
        return 2;
    }
}
