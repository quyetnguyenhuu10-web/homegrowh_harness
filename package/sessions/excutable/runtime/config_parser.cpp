#include "config_parser.h"

#include <config.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace sessions_runtime
{
    namespace
    {
        const nlohmann::json& required_field(
            const nlohmann::json& object,
            const char* key)
        {
            if (!object.is_object())
                throw std::invalid_argument("session config must be an object");

            const auto found = object.find(key);
            if (found == object.end())
            {
                throw std::invalid_argument(
                    std::string("missing session config field: ") + key);
            }
            return *found;
        }

        std::string required_string(
            const nlohmann::json& object,
            const char* key,
            bool allow_empty = false)
        {
            const nlohmann::json& value = required_field(object, key);
            if (!value.is_string())
            {
                throw std::invalid_argument(
                    std::string(key) + " must be a string");
            }

            std::string result = value.get<std::string>();
            if (!allow_empty && result.empty())
            {
                throw std::invalid_argument(
                    std::string(key) + " must not be empty");
            }
            return result;
        }

        std::uint64_t required_uint64(
            const nlohmann::json& object,
            const char* key)
        {
            const nlohmann::json& value = required_field(object, key);
            if (value.is_number_unsigned())
                return value.get<std::uint64_t>();

            if (value.is_number_integer())
            {
                const std::int64_t signed_value =
                    value.get<std::int64_t>();
                if (signed_value >= 0)
                    return static_cast<std::uint64_t>(signed_value);
            }

            throw std::invalid_argument(
                std::string(key) + " must be an unsigned integer");
        }

        int required_int(
            const nlohmann::json& object,
            const char* key)
        {
            const nlohmann::json& value = required_field(object, key);
            if (!value.is_number_integer())
            {
                throw std::invalid_argument(
                    std::string(key) + " must be an integer");
            }

            const std::int64_t result = value.get<std::int64_t>();
            if (
                result < (std::numeric_limits<int>::min)() ||
                result > (std::numeric_limits<int>::max)())
            {
                throw std::out_of_range(
                    std::string(key) + " does not fit int");
            }
            return static_cast<int>(result);
        }

        bool required_bool(
            const nlohmann::json& object,
            const char* key)
        {
            const nlohmann::json& value = required_field(object, key);
            if (!value.is_boolean())
            {
                throw std::invalid_argument(
                    std::string(key) + " must be a boolean");
            }
            return value.get<bool>();
        }

        nlohmann::json required_array(
            const nlohmann::json& object,
            const char* key)
        {
            const nlohmann::json& value = required_field(object, key);
            if (!value.is_array())
            {
                throw std::invalid_argument(
                    std::string(key) + " must be an array");
            }
            return value;
        }

        sandbox::config parse_sandbox_config(
            const nlohmann::json& input)
        {
            if (!input.is_object())
            {
                throw std::invalid_argument(
                    "sandbox_config must be an object");
            }

            sandbox::config result;

            const auto add_paths = [&](const char* key, bool write)
            {
                const nlohmann::json& paths = required_field(input, key);
                if (!paths.is_array())
                {
                    throw std::invalid_argument(
                        std::string("sandbox_config.") + key +
                        " must be an array");
                }

                for (const nlohmann::json& item : paths)
                {
                    if (!item.is_string())
                    {
                        throw std::invalid_argument(
                            std::string("sandbox_config.") + key +
                            " entries must be strings");
                    }

                    std::filesystem::path path =
                        item.get<std::string>();
                    result.add(
                        write
                            ? sandbox::config_option(
                                sandbox::read_write(std::move(path)))
                            : sandbox::config_option(
                                sandbox::read_only(std::move(path))));
                }
            };

            add_paths("read_only", false);
            add_paths("read_write", true);

            const std::string network =
                required_string(input, "network");
            if (network == "none")
            {
                result.add(sandbox::network(
                    sandbox::network_access::none));
            }
            else if (network == "internet_client")
            {
                result.add(sandbox::network(
                    sandbox::network_access::internet_client));
            }
            else
            {
                throw std::invalid_argument(
                    "sandbox_config.network must be none or internet_client");
            }

            return result;
        }
    }

    sessions::SessionConfig parse_session_config(
        nlohmann::json&& input)
    {
        sessions::SessionConfig config;
        config.api_key_raw = required_string(input, "api_key_raw");
        config.history = required_array(input, "history");
        config.session_current = required_field(input, "session_current");
        config.tool_definitions = required_array(input, "tool_definitions");
        config.provider = provider::provider_from_name(
            required_string(input, "provider"));
        config.endpoint = required_string(input, "endpoint");
        config.model_id = required_string(input, "model_id");
        config.context_limit = required_uint64(input, "context_limit");
        config.compact_threshold = required_uint64(
            input,
            "compact_threshold");
        config.tool_result_timeout_ms = required_int(
            input,
            "tool_result_timeout_ms");
        config.session_timeout_ms = required_int(
            input,
            "session_timeout_ms");
        config.compaction_prompt = required_string(
            input,
            "compaction_prompt",
            true);
        config.workspace_path = required_string(input, "workspace_path");
        config.tool_runtime_executable = required_string(
            input,
            "tool_runtime_executable");
        config.sandbox_config = parse_sandbox_config(
            required_field(input, "sandbox_config"));
        config.refresh_workspace = required_bool(
            input,
            "refresh_workspace");
        return config;
    }
}
