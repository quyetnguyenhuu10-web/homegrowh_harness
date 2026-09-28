#include <sessions>
#include "util/terminal_event_ui.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
    nlohmann::json load_json_file(
        const std::filesystem::path& path,
        const char* label)
    {
        std::ifstream file(path);
        if (!file)
        {
            throw std::runtime_error(
                std::string("cannot open ") + label + " file: " +
                path.string());
        }

        nlohmann::json result;
        file >> result;
        return result;
    }

    const nlohmann::json& required_field(
        const nlohmann::json& config,
        const char* key)
    {
        const auto found = config.find(key);
        if (found == config.end())
            throw std::invalid_argument(std::string("missing config field: ") + key);
        return *found;
    }

    std::string required_string(
        const nlohmann::json& config,
        const char* key)
    {
        const nlohmann::json& value = required_field(config, key);
        if (!value.is_string() || value.get_ref<const std::string&>().empty())
            throw std::invalid_argument(std::string(key) + " must be a non-empty string");
        return value.get<std::string>();
    }

    std::uint64_t required_uint64(
        const nlohmann::json& config,
        const char* key)
    {
        const nlohmann::json& value = required_field(config, key);
        if (!value.is_number_unsigned())
            throw std::invalid_argument(std::string(key) + " must be an unsigned integer");
        return value.get<std::uint64_t>();
    }

    int optional_int(
        const nlohmann::json& config,
        const char* key,
        int fallback)
    {
        const auto found = config.find(key);
        if (found == config.end())
            return fallback;
        if (!found->is_number_integer())
            throw std::invalid_argument(std::string(key) + " must be an integer");
        return found->get<int>();
    }

    bool optional_bool(
        const nlohmann::json& config,
        const char* key,
        bool fallback)
    {
        const auto found = config.find(key);
        if (found == config.end())
            return fallback;
        if (!found->is_boolean())
            throw std::invalid_argument(std::string(key) + " must be a boolean");
        return found->get<bool>();
    }

    std::string load_text_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            throw std::runtime_error(
                "cannot open text file: " + path.string());
        }

        return std::string(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
    }

    nlohmann::json json_value_or_file(
        const nlohmann::json& value,
        const char* label)
    {
        if (!value.is_string())
            return value;

        const std::filesystem::path path =
            value.get_ref<const std::string&>();
        return load_json_file(path, label);
    }

    sandbox::config load_sandbox_config(const nlohmann::json& input)
    {
        if (!input.is_object())
        {
            throw std::invalid_argument(
                "sandbox_config must be a JSON object");
        }

        sandbox::config config;

        const auto add_paths = [&](const char* key, bool read_write)
        {
            const auto paths = input.find(key);
            if (paths == input.end())
                return;
            if (!paths->is_array())
            {
                throw std::invalid_argument(
                    std::string("sandbox_config.") + key +
                    " must be an array");
            }

            for (const nlohmann::json& item : *paths)
            {
                if (!item.is_string())
                {
                    throw std::invalid_argument(
                        std::string("sandbox_config.") + key +
                        " entries must be strings");
                }

                const std::filesystem::path path =
                    item.get_ref<const std::string&>();
                config.add(
                    read_write
                        ? sandbox::config_option(sandbox::read_write(path))
                        : sandbox::config_option(sandbox::read_only(path)));
            }
        };

        add_paths("read_only", false);
        add_paths("read_write", true);

        const auto network = input.find("network");
        if (network == input.end() || !network->is_string())
        {
            throw std::invalid_argument(
                "sandbox_config.network must be none or internet_client");
        }

        const std::string& network_value =
            network->get_ref<const std::string&>();
        if (network_value == "none")
        {
            config.add(sandbox::network(
                sandbox::network_access::none));
        }
        else if (network_value == "internet_client")
        {
            config.add(sandbox::network(
                sandbox::network_access::internet_client));
        }
        else
        {
            throw std::invalid_argument(
                "sandbox_config.network must be none or internet_client");
        }

        return config;
    }

    std::filesystem::path load_workspace_path(const std::string& value)
    {
        const std::filesystem::path path(value);
        std::error_code error;
        if (!std::filesystem::is_directory(path, error))
        {
            if (error)
            {
                throw std::filesystem::filesystem_error(
                    "cannot inspect workspace",
                    path,
                    error);
            }

            throw std::filesystem::filesystem_error(
                "workspace is not a directory",
                path,
                std::make_error_code(std::errc::not_a_directory));
        }

        const std::filesystem::path canonical =
            std::filesystem::canonical(path, error);
        if (error)
        {
            throw std::filesystem::filesystem_error(
                "cannot canonicalize workspace",
                path,
                error);
        }
        return canonical;
    }

    void print_usage(const char* executable)
    {
        std::cerr
            << "usage:\n"
            << "  " << executable
            << " <config.json>\n";
    }
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc != 2)
    {
        print_usage(argv[0]);
        return 2;
    }

    try
    {
        const nlohmann::json input =
            load_json_file(argv[1], "session config");
        if (!input.is_object())
            throw std::invalid_argument("session config root must be an object");

        std::string api_key = required_string(input, "api_key");
        const provider::Provider selected_provider =
            provider::provider_from_name(required_string(input, "provider"));
        std::string endpoint = required_string(input, "endpoint");
        std::string model_id = required_string(input, "model_id");
        const std::uint64_t context_limit =
            required_uint64(input, "context_limit");
        const std::uint64_t compact_threshold =
            required_uint64(input, "compact_threshold");

        nlohmann::json history = nlohmann::json::array();
        if (const auto found = input.find("history"); found != input.end())
            history = json_value_or_file(*found, "history");

        nlohmann::json session_current = json_value_or_file(
            required_field(input, "session_current"),
            "session_current");
        nlohmann::json tool_definitions = json_value_or_file(
            required_field(input, "tool_definitions"),
            "tool_definitions");

        const std::filesystem::path workspace_path =
            load_workspace_path(required_string(input, "workspace_path"));
        const std::filesystem::path tool_runtime_executable =
            required_string(input, "tool_runtime_executable");

        std::string compaction_prompt = load_text_file(
            required_string(input, "compaction_prompt_path"));
        sandbox::config sandbox_config = load_sandbox_config(
            required_field(input, "sandbox_config"));
        const bool refresh_workspace =
            optional_bool(input, "refresh_workspace", false);
        const int tool_result_timeout_ms =
            optional_int(input, "tool_result_timeout_ms", -1);
        const int session_timeout_ms =
            optional_int(input, "session_timeout_ms", -1);

        sessions_loop::util::TerminalEventUi terminal(selected_provider);

        sessions::SessionConfig config;
        config.api_key_raw = std::move(api_key);
        config.history = std::move(history);
        config.session_current = std::move(session_current);
        config.tool_definitions = std::move(tool_definitions);
        config.provider = selected_provider;
        config.endpoint = std::move(endpoint);
        config.model_id = model_id;
        config.context_limit = context_limit;
        config.compact_threshold = compact_threshold;
        config.tool_result_timeout_ms = tool_result_timeout_ms;
        config.session_timeout_ms = session_timeout_ms;
        config.compaction_prompt = std::move(compaction_prompt);
        config.workspace_path = workspace_path;
        config.tool_runtime_executable = tool_runtime_executable;
        config.sandbox_config = std::move(sandbox_config);
        config.refresh_workspace = refresh_workspace;
        (void)sessions::loop(std::move(config));

        terminal.stop();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "loop error: " << error.what() << '\n';
        return 1;
    }
}
