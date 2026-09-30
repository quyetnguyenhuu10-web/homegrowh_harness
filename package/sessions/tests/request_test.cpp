#include <context_usage>
#include <context/context.h>
#include <provider>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <variant>

namespace
{
    void require(bool value, const char* message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    template <typename Exception, typename Callable>
    void require_throws(Callable&& callable, const char* message)
    {
        try
        {
            callable();
        }
        catch (const Exception&)
        {
            return;
        }

        throw std::runtime_error(message);
    }
}

int main()
{
    using nlohmann::json;

    require(
        !sessions::detail::should_compact(799, 0, 1000),
        "usage below custom threshold must not compact");
    require(
        !sessions::detail::should_compact(1000, 0, 1000),
        "usage at custom threshold must not compact");
    require(
        sessions::detail::should_compact(1001, 0, 1000),
        "usage above custom threshold must compact");

    const json session_current = {
        {"messages", json::array({
            {
                {"role", "user"},
                {"content", std::string(400, 'x')}
            }
        })}
    };
    const std::uint64_t session_estimate =
        context_usage::estimate(session_current);
    require(session_estimate > 0, "session estimate must not be zero");
    require(session_estimate < 700, "session estimate must fit test threshold");
    const std::uint64_t exact_threshold_checkpoint = 700 - session_estimate;
    require(
        !sessions::detail::should_compact(
            exact_threshold_checkpoint,
            session_estimate,
            700),
        "session estimate at exact threshold must not compact");
    require(
        sessions::detail::should_compact(
            exact_threshold_checkpoint + 1,
            session_estimate,
            700),
        "session estimate must contribute to compaction decision");

    provider::CompactionResponse compaction_response;
    require(
        std::holds_alternative<provider::UsageState>(compaction_response.usage),
        "compaction request usage must default to unavailable");

    require_throws<std::overflow_error>(
        []
        {
            (void)sessions::detail::should_compact(
                std::numeric_limits<std::uint64_t>::max(),
                1,
                1000);
        },
        "usage addition overflow must fail");

    require(
        sessions::detail::should_compact(951, 0, 800),
        "core must not enforce a 95 percent safety policy");
    require(
        !sessions::detail::should_compact(951, 0, 2000),
        "caller-provided threshold must fully control compaction");

    const auto invalid_compaction = []
        {
            const json history = json::array({
                {
                    {"role", "user"},
                    {"content", "history"}
                }
            });

            return provider::compaction(
                provider::Provider::bonsai,
                "http://unused.invalid/v1/chat/completions",
                "bonsai",
                "unused",
                "summary prompt",
                json::object(),
                json::array(),
                history,
                {},
                true,
                {},
                nullptr);
        }();
    require(invalid_compaction.error && !invalid_compaction.value,
        "compact without current messages or tools must return an error before summary request");
    require(invalid_compaction.error->type == "invalid_argument",
        "compaction validation must preserve the error type");

    std::cout << "sessions request tests passed\n";
    return 0;
}
