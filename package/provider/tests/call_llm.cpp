#include <provider>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    void stream_text(std::string_view event)
    {
        const nlohmann::json payload = nlohmann::json::parse(
            event,
            nullptr,
            false);

        if (payload.is_discarded())
        {
            return;
        }

        const auto choices = payload.find("choices");

        if (choices == payload.end() || !choices->is_array())
        {
            return;
        }

        for (const nlohmann::json& choice : *choices)
        {
            const auto delta = choice.find("delta");

            if (delta == choice.end() || !delta->is_object())
            {
                continue;
            }

            const auto content = delta->find("content");

            if (
                content != delta->end() &&
                content->is_string())
            {
                std::cout << content->get_ref<const std::string&>();
                std::cout.flush();
            }
        }
    }

    void print_optional(
        const char* name,
        const std::optional<std::uint64_t>& value)
    {
        if (value.has_value())
        {
            std::cout << name << ": " << *value << '\n';
        }
    }

    void print_usage(const provider::RequestUsage& usage)
    {
        std::cout << "\n\n===== USAGE SUMMARY =====\n";

        if (const auto* openai = std::get_if<provider::OpenAIUsage>(&usage))
        {
            std::cout << "provider: openai\n";
            std::cout << "input_tokens: " << openai->prompt_tokens << '\n';
            std::cout << "output_tokens: " << openai->completion_tokens << '\n';
            std::cout << "total_tokens: " << openai->total_tokens << '\n';

            if (openai->prompt_tokens_details.has_value())
            {
                print_optional(
                    "cached_tokens",
                    openai->prompt_tokens_details->cached_tokens);
                print_optional(
                    "cache_write_tokens",
                    openai->prompt_tokens_details->cache_write_tokens);
            }

            if (openai->completion_tokens_details.has_value())
            {
                print_optional(
                    "reasoning_tokens",
                    openai->completion_tokens_details->reasoning_tokens);
            }
        }
        else if (const auto* deepseek = std::get_if<provider::DeepSeekUsage>(&usage))
        {
            std::cout << "provider: deepseek\n";
            std::cout << "input_tokens: " << deepseek->prompt_tokens << '\n';
            std::cout << "cache_hit_tokens: "
                      << deepseek->prompt_cache_hit_tokens
                      << '\n';
            std::cout << "cache_miss_tokens: "
                      << deepseek->prompt_cache_miss_tokens
                      << '\n';
            std::cout << "output_tokens: " << deepseek->completion_tokens << '\n';
            std::cout << "total_tokens: " << deepseek->total_tokens << '\n';

            if (deepseek->completion_tokens_details.has_value())
            {
                print_optional(
                    "reasoning_tokens",
                    deepseek->completion_tokens_details->reasoning_tokens);
            }
        }
        else if (const auto* bonsai = std::get_if<provider::BonsaiUsage>(&usage))
        {
            std::cout << "provider: bonsai\n";
            std::cout << "input_tokens: "
                      << bonsai->cache_n + bonsai->prompt_n
                      << '\n';
            std::cout << "cached_tokens: " << bonsai->cache_n << '\n';
            std::cout << "prompt_tokens_evaluated: " << bonsai->prompt_n << '\n';
            std::cout << "output_tokens: " << bonsai->predicted_n << '\n';
            std::cout << "prompt_ms: " << bonsai->prompt_ms << '\n';
            std::cout << "prompt_tokens_per_second: "
                      << bonsai->prompt_per_second
                      << '\n';
            std::cout << "generation_ms: " << bonsai->predicted_ms << '\n';
            std::cout << "generation_tokens_per_second: "
                      << bonsai->predicted_per_second
                      << '\n';
        }
        else if (std::holds_alternative<provider::UsageState>(usage))
        {
            std::cout << "usage: unavailable\n";
        }

        std::cout << "=========================\n";
    }
}

int main()
{
    const std::string url = "https://trickle-crewless-smasher.ngrok-free.dev/v1/chat/completions";
    const std::string api_key = "Bearer NgKFxGR8k3w2LtkMzgrHM04cCdgUx0EPbF4HoOGKTTw";
    const std::string model = "bonsai";

    const nlohmann::json messages = nlohmann::json::array({
        {
            {"role", "user"},
            {"content", "Bạn có tool gì"}
        }
    });

    std::ifstream tool_file("../tools/src/tool_definitions.json");
    if (!tool_file)
    {
        std::cerr << "Cannot open ../tools/src/tool_definitions.json\n";
        return 1;
    }

    nlohmann::json tools;
    tool_file >> tools;

    const nlohmann::json body = {
        {"model", model},
        {"messages", messages},
        {"tools", tools},
        {"stream", true}
    };

    const bool benchmark = std::getenv("PROVIDER_BENCHMARK") != nullptr;
    using clock = std::chrono::steady_clock;
    const auto started = clock::now();

    try
    {
        provider::RawResponse raw_response;
        provider::CompletionPort completion_port;
        std::exception_ptr request_error;
        std::optional<provider::RequestUsage> usage;
        std::size_t events = 0;
        std::size_t payload_bytes = 0;
        bool have_first_event = false;
        clock::time_point first_event_at{};

        completion_port.register_response(&raw_response);

        if (!benchmark)
        {
            std::cout << "===== RESPONSE =====\n";
        }

        std::thread provider_thread([&]
        {
            try
            {
                usage = provider::request(
                    url,
                    api_key,
                    body,
                    &raw_response);
            }
            catch (...)
            {
                request_error = std::current_exception();
            }
        });

        std::thread output_thread([&]
        {
            bool finished = false;

            while (!finished)
            {
                provider::Completion completion;

                completion_port.wait(&completion);

                if (
                    completion.type == provider::CompletionType::finished ||
                    completion.type == provider::CompletionType::failed)
                {
                    if (completion.type == provider::CompletionType::failed)
                    {
                        std::cerr << "provider error="
                                  << completion.error
                                  << '\n';
                    }

                    finished = true;
                    continue;
                }

                if (!have_first_event)
                {
                    first_event_at = clock::now();
                    have_first_event = true;
                }

                provider::read(
                    completion.response,
                    [&](std::string_view buffer)
                    {
                        const std::string_view delta = buffer.substr(
                            completion.offset,
                            completion.bytes);

                        ++events;
                        payload_bytes += delta.size();

                        if (benchmark)
                        {
                            return;
                        }

                        stream_text(delta);
                    });
            }
        });

        provider_thread.join();
        output_thread.join();

        if (request_error)
        {
            std::rethrow_exception(request_error);
        }

        if (!usage.has_value())
        {
            throw std::runtime_error("request completed without usage");
        }

        print_usage(*usage);

        if (benchmark)
        {
            const auto finished_at = clock::now();
            const auto milliseconds = [](clock::duration duration)
            {
                return std::chrono::duration<double, std::milli>(duration).count();
            };

            if (have_first_event)
            {
                std::cout << "first_event_ms="
                          << milliseconds(first_event_at - started)
                          << '\n';
            }
            else
            {
                std::cout << "first_event_ms=none\n";
            }

            std::cout << "total_ms="
                      << milliseconds(finished_at - started)
                      << '\n';
            std::cout << "events=" << events << '\n';
            std::cout << "payload_bytes=" << payload_bytes << '\n';
            return 0;
        }

        std::cout << '\n';
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
