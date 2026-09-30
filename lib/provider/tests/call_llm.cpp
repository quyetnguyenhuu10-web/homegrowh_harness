#include <provider>
#include "../src/error/capture.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

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

int report(const provider::Error& error)
{
    auto encoded = provider::serialize_error(error);
    if (encoded)
    {
        std::cerr << encoded.value->dump() << '\n';
    }
    return 1;
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 5 || argc > 6)
        {
            return report(provider::error_detail::make_error(
                "call_llm", "invalid_argument",
                "Expected provider, URL, model, prompt and optional tools JSON path",
                {{{"argc", argc}}}));
        }
        auto selected = provider::provider_from_name(argv[1]);
        if (!selected)
        {
            return report(*selected.error);
        }
        const std::string url = argv[2];
        const char* raw_api_key = std::getenv("HH_API_KEY");
        if (raw_api_key == nullptr || *raw_api_key == '\0')
        {
            return report(provider::error_detail::make_error(
                "call_llm", "invalid_argument", "HH_API_KEY is not set",
                {{{"environment_variable", "HH_API_KEY"}}}));
        }
        const std::string api_key = raw_api_key;
        nlohmann::json tools = nlohmann::json::array();
        if (argc == 6)
        {
            errno = 0;
            std::ifstream input(argv[5]);
            const int native_error = errno;
            if (!input)
            {
                nlohmann::json details = {{"path", argv[5]}, {"api", "std::ifstream::open"}};
                if (native_error != 0)
                {
                    details["code"] = native_error;
                    details["category"] = "errno";
                }
                return report(provider::error_detail::make_error(
                    "read_tools", "system_error", "Tools file stream reports a failure",
                    {std::move(details)}));
            }
            input >> tools;
        }
        const nlohmann::json body = {
            {"model", argv[3]},
            {"messages", nlohmann::json::array({{{"role", "user"}, {"content", argv[4]}}})},
            {"tools", std::move(tools)}, {"stream", true}};
        const bool benchmark = std::getenv("PROVIDER_BENCHMARK") != nullptr;
        using clock = std::chrono::steady_clock;
        const auto started = clock::now();

        std::size_t events = 0;
        std::size_t payload_bytes = 0;
        bool have_first_event = false;
        clock::time_point first_event_at{};

        if (!benchmark)
        {
            std::cout << "===== RESPONSE =====\n";
        }

        struct StreamContext
        {
            bool benchmark;
            std::size_t* events;
            std::size_t* payload_bytes;
            bool* have_first_event;
            clock::time_point* first_event_at;
        } context{
            benchmark,
            &events,
            &payload_bytes,
            &have_first_event,
            &first_event_at
        };

        auto requested = provider::request(
            *selected.value,
            url,
            api_key,
            body,
            provider::EventSink{
                &context,
                [](void* raw_context, std::string&& event)
                {
                    auto* context = static_cast<StreamContext*>(raw_context);

                    if (!*context->have_first_event)
                    {
                        *context->first_event_at = clock::now();
                        *context->have_first_event = true;
                    }

                    ++*context->events;
                    *context->payload_bytes += event.size();

                    if (!context->benchmark)
                    {
                        stream_text(event);
                    }
                    return provider::Result<void>::success();
                },
                nullptr
            });

        if (!requested)
        {
            return report(*requested.error);
        }
        print_usage(*requested.value);

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
    catch (...)
    {
        return report(provider::error_detail::capture_exception(
            std::current_exception(), "call_llm"));
    }

    return 0;
}
