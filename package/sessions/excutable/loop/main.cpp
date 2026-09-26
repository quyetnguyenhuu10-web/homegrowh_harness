#include <sessions>

#include <cstdlib>
#include <charconv>
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
    std::filesystem::path tool_definitions_path()
    {
        if (const char* explicit_path = std::getenv("TOOLS_DEFINITIONS"))
        {
            if (*explicit_path == '\0')
                throw std::runtime_error("TOOLS_DEFINITIONS is empty");
            return explicit_path;
        }

        std::filesystem::path directory = std::filesystem::current_path();
        while (true)
        {
            const std::filesystem::path candidates[] = {
                directory / "tools" / "src" / "tool_definitions.json",
                directory / "package" / "tools" / "src" / "tool_definitions.json"
            };

            for (const std::filesystem::path& candidate : candidates)
            {
                if (std::filesystem::is_regular_file(candidate))
                    return candidate;
            }

            const std::filesystem::path parent = directory.parent_path();
            if (parent == directory || parent.empty())
                break;
            directory = parent;
        }

        throw std::runtime_error("tool_definitions.json not found");
    }

    nlohmann::json load_tool_definitions()
    {
        const std::filesystem::path path = tool_definitions_path();
        std::ifstream file(path);
        if (!file)
            throw std::runtime_error("cannot open tool_definitions.json");

        nlohmann::json definitions;
        file >> definitions;
        if (!definitions.is_array())
            throw std::runtime_error("tool_definitions.json root must be an array");
        return definitions;
    }

    std::string required_environment(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0')
        {
            throw std::runtime_error(std::string(name) + " is not set");
        }
        return value;
    }

    std::uint64_t required_uint64_environment(const char* name)
    {
        const std::string value = required_environment(name);
        std::uint64_t result = 0;
        const auto parsed = std::from_chars(
            value.data(),
            value.data() + value.size(),
            result);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        {
            throw std::invalid_argument(
                std::string(name) + " must be an unsigned integer");
        }
        return result;
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

    bool parse_bool(std::string_view value)
    {
        if (value == "true" || value == "1")
            return true;
        if (value == "false" || value == "0")
            return false;
        throw std::invalid_argument("refresh must be true, false, 1, or 0");
    }

    bool refresh_workspace_from_environment()
    {
        const char* value = std::getenv("HH_REFRESH_WORKSPACE");
        if (value == nullptr || *value == '\0')
            return false;
        return parse_bool(value);
    }

    nlohmann::json load_json_argument(
        const std::string& value,
        const char* label)
    {
        const std::filesystem::path path(value);
        std::error_code error;
        const bool is_file = std::filesystem::is_regular_file(path, error);
        if (!error && is_file)
        {
            std::ifstream file(path);
            if (!file)
            {
                throw std::runtime_error(
                    std::string("cannot open ") + label + " file: " + value);
            }

            nlohmann::json result;
            file >> result;
            return result;
        }

        nlohmann::json result = nlohmann::json::parse(
            value,
            nullptr,
            false);
        if (result.is_discarded())
        {
            throw std::invalid_argument(
                std::string(label) +
                " must be inline JSON or a path to a JSON file");
        }
        return result;
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

    class TerminalStream final
    {
    public:
        void operator()(sessions::StreamType type, std::string_view value)
        {
            switch (type)
            {
                case sessions::StreamType::reasoning:
                    begin_text(TextMode::reasoning, "[reasoning] ");
                    std::cout << value;
                    std::cout.flush();
                    return;

                case sessions::StreamType::content:
                    begin_text(TextMode::content, "[content] ");
                    std::cout << value;
                    std::cout.flush();
                    return;

                case sessions::StreamType::summary_start:
                    finish_text();
                    std::cout << "[summary]\n";
                    std::cout.flush();
                    return;

                case sessions::StreamType::summary_reasoning:
                    begin_text(TextMode::reasoning, "[reasoning] ");
                    std::cout << value;
                    std::cout.flush();
                    return;

                case sessions::StreamType::summary_content:
                    begin_text(TextMode::content, "[content] ");
                    std::cout << value;
                    std::cout.flush();
                    return;

                case sessions::StreamType::summary_end:
                    finish_text();
                    std::cout << "[/summary]\n";
                    std::cout.flush();
                    return;

                case sessions::StreamType::http_error:
                {
                    finish_text();
                    const nlohmann::json payload = nlohmann::json::parse(value);
                    const std::string phase = payload.value(
                        "phase",
                        std::string("request"));
                    const long status_code = payload.value("status_code", 0L);
                    const std::string status_line = payload.value(
                        "status_line",
                        std::string{});
                    const std::string reason = payload.value(
                        "reason",
                        std::string{});
                    const std::string body = payload.value(
                        "body",
                        std::string{});

                    std::cerr << "HTTP error [" << phase << "]\n";
                    if (!status_line.empty())
                    {
                        std::cerr << "status: " << status_line << '\n';
                    }
                    else
                    {
                        std::cerr << "status: " << status_code;
                        if (!reason.empty())
                            std::cerr << ' ' << reason;
                        std::cerr << '\n';
                    }

                    if (!body.empty())
                        std::cerr << "body: " << body << '\n';
                    std::cerr.flush();
                    return;
                }

                case sessions::StreamType::secondary_error:
                {
                    finish_text();
                    const nlohmann::json payload = nlohmann::json::parse(value);
                    std::cerr
                        << "Secondary error ["
                        << payload.value("source", std::string("unknown"))
                        << "]";

                    const std::string operation = payload.value(
                        "operation",
                        std::string{});
                    if (!operation.empty())
                        std::cerr << " operation=" << operation;

                    if (payload.contains("code"))
                        std::cerr << " code=" << payload.at("code");

                    const std::string exception = payload.value(
                        "exception",
                        std::string{});
                    if (!exception.empty())
                        std::cerr << " exception=" << exception;

                    std::cerr << '\n';
                    std::cerr.flush();
                    return;
                }

                case sessions::StreamType::tool_call:
                    finish_text();
                    std::cout << "Đã gọi tool với id: " << value << '\n';
                    std::cout.flush();
                    return;

                case sessions::StreamType::tool_result:
                    finish_text();
                    std::cout << "Tool result của id: " << value << '\n';
                    std::cout.flush();
                    return;

                case sessions::StreamType::context_usage:
                {
                    finish_text();
                    const nlohmann::json payload = nlohmann::json::parse(value);
                    const std::uint64_t used = payload.at("used").get<std::uint64_t>();
                    const std::uint64_t limit = payload.at("limit").get<std::uint64_t>();
                    const double ratio = limit == 0
                        ? 0.0
                        : (static_cast<double>(used) * 100.0) /
                            static_cast<double>(limit);

                    std::cout
                        << "Context usage: "
                        << used
                        << " / "
                        << limit
                        << " ("
                        << ratio
                        << "%)\n";
                    std::cout.flush();
                    return;
                }
            }
        }

        void finish()
        {
            finish_text();
        }

    private:
        enum class TextMode
        {
            none,
            reasoning,
            content,
        };

        void begin_text(TextMode mode, const char* label)
        {
            if (mode_ == mode)
                return;

            finish_text();
            std::cout << label;
            mode_ = mode;
        }

        void finish_text()
        {
            if (mode_ == TextMode::none)
                return;

            std::cout << '\n';
            mode_ = TextMode::none;
        }

        TextMode mode_ = TextMode::none;
    };

    void print_usage(const char* executable)
    {
        std::cerr
            << "usage:\n"
            << "  " << executable
            << " <session_current> <id> <workspace_path>\n"
            << "  " << executable
            << " <history> <session_current> <id> <workspace_path>\n"
            << "history is optional; when omitted it defaults to []\n"
            << "history/session_current: inline JSON or path to JSON file\n"
            << "environment:\n"
            << "  HH_API_KEY=<raw api key>\n"
            << "  HH_PROVIDER=<openai|deepseek|bonsai>\n"
            << "  HH_ENDPOINT=<request endpoint>\n"
            << "  HH_CONTEXT_LIMIT=<tokens>\n"
            << "  HH_COMPACT_THRESHOLD=<tokens>\n"
            << "  PROVIDER_COMPACTION_PROMPT=<prompt file path>\n"
            << "  HH_REFRESH_WORKSPACE=<true|false> (default false)\n";
    }
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc != 4 && argc != 5)
    {
        print_usage(argv[0]);
        return 2;
    }

    try
    {
        const char* raw_api_key = std::getenv("HH_API_KEY");
        if (raw_api_key == nullptr || *raw_api_key == '\0')
            throw std::runtime_error("HH_API_KEY is not set");

        const bool has_history = argc == 5;
        nlohmann::json history = has_history
            ? load_json_argument(argv[1], "history")
            : nlohmann::json::array();
        const int session_current_index = has_history ? 2 : 1;
        const int model_id_index = has_history ? 3 : 2;
        const int workspace_path_index = has_history ? 4 : 3;

        nlohmann::json session_current =
            load_json_argument(argv[session_current_index], "session_current");
        const std::string model_id = argv[model_id_index];
        const std::filesystem::path workspace_path =
            load_workspace_path(argv[workspace_path_index]);
        const bool refresh_workspace = refresh_workspace_from_environment();

        nlohmann::json tool_definitions = load_tool_definitions();
        std::string api_key(raw_api_key);
        const provider::Provider selected_provider = provider::provider_from_name(
            required_environment("HH_PROVIDER"));
        std::string endpoint = required_environment("HH_ENDPOINT");
        const std::uint64_t context_limit =
            required_uint64_environment("HH_CONTEXT_LIMIT");
        const std::uint64_t compact_threshold =
            required_uint64_environment("HH_COMPACT_THRESHOLD");
        std::string compaction_prompt = load_text_file(
            required_environment("PROVIDER_COMPACTION_PROMPT"));
        TerminalStream terminal;

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
        config.compaction_prompt = std::move(compaction_prompt);
        config.workspace_path = workspace_path;
        config.refresh_workspace = refresh_workspace;
        config.stream = [&](sessions::StreamType type, std::string_view value)
        {
            terminal(type, value);
        };

        (void)sessions::loop(std::move(config));

        terminal.finish();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "loop error: " << error.what() << '\n';
        return 1;
    }
}
