#include <session>
#include <event_port>
#include <error/event_port.h>
#include <request/request.h>
#include <request/turn.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>

namespace
{
    using nlohmann::json;
    std::exception_ptr request_failure;
    bool finish_summary = false;
    bool print_wire = false;

    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void write_event(const event_port::EventPtr& event)
    {
        require(event != nullptr, "Missing provider event");
        if (print_wire)
        {
            json references = json::array();
            for (const auto& reference : event->references)
                references.push_back(json::array({reference.type, reference.value}));
            const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                event->timestamp.time_since_epoch()).count();
            std::cout << json::array({event->sequence, timestamp, event->package,
                static_cast<int>(event->level), event->type, std::move(references), event->data}).dump() << '\n';
        }
    }

    void check_turn(const std::exception_ptr& failure, bool compact, bool summary_finished)
    {
        request_failure = failure;
        finish_summary = summary_finished;
        auto registration = sessions::detail::checked_port(event_port::Register{"provider", {}});
        std::vector<event_port::EventPtr> events;
        std::exception_ptr read_failure;
        // EventPort applies backpressure, so consume while run_turn publishes.
        std::jthread reader([&]
        {
            try
            {
                for (;;)
                {
                    auto next = event_port::port(event_port::Read{registration});
                    if (next.error)
                    {
                        if (next.error->type == "registration_closed")
                            return;
                        throw sessions::ErrorException(
                            sessions::detail::convert_error<sessions::Error>(std::move(*next.error)));
                    }
                    events.push_back(std::move(*next.value));
                }
            }
            catch (...)
            {
                read_failure = std::current_exception();
                (void)event_port::port(event_port::Close{registration});
            }
        });
        std::exception_ptr propagated;
        try
        {
            (void)sessions::detail::run_turn("fixture", "unused", "fixture", provider::Provider::openai,
                "summary", json::object(), json::array(), compact,
                compact ? json::array({json{{"role", "user"}, {"content", "fixture"}}}) : json::array());
        }
        catch (...)
        {
            propagated = std::current_exception();
        }
        const auto closed = event_port::port(event_port::Close{registration});
        reader.join();
        if (read_failure != nullptr)
            std::rethrow_exception(read_failure);
        require(!closed.error, "Provider test registration could not be closed");
        require(events.size() == (compact ? (summary_finished ? 3 : 2) : 1),
            "Provider event count was changed");
        for (const auto& event : events)
            write_event(event);
        require(propagated != nullptr, "run_turn must propagate the request failure");
        // rethrow_exception may copy the object; compare the original type and payload.
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::exception& original)
        {
            try
            {
                std::rethrow_exception(propagated);
            }
            catch (const std::exception& actual)
            {
                require(typeid(actual) == typeid(original), "run_turn changed the exception type");
                require(std::string_view(actual.what()) == original.what(), "run_turn changed the exception message");
                if (const auto* system = dynamic_cast<const std::system_error*>(&original))
                    require(dynamic_cast<const std::system_error&>(actual).code() == system->code(),
                        "run_turn changed the exception code");
                if (const auto* filesystem = dynamic_cast<const std::filesystem::filesystem_error*>(&original))
                {
                    const auto& actual_filesystem = dynamic_cast<const std::filesystem::filesystem_error&>(actual);
                    require(actual_filesystem.path1() == filesystem->path1() &&
                        actual_filesystem.path2() == filesystem->path2(), "run_turn changed the exception paths");
                }
            }
        }
        catch (int original)
        {
            try
            {
                std::rethrow_exception(propagated);
            }
            catch (int actual)
            {
                require(actual == original, "run_turn changed the non-standard exception");
            }
        }
        if (compact)
        {
            require(events.at(0)->type == "started", "Summary started event was lost");
            if (summary_finished)
                require(events.at(1)->type == "finished", "Summary finished event was lost");
        }
        const auto& event = events.back();
        require(event->type == "failed" && event->level == event_port::Level::error,
            "Expected provider failed event");
        require(event->data.at("phase") == (compact && !summary_finished ? "summary" : "request"),
            "Failing provider phase was changed");
        require(!event->data.contains("raw"), "Provider failure still contains legacy raw");
        const auto& error = event->data.at("error");
        require(error.size() == 6 && error.at("data").is_array() && error.at("causes").is_array(),
            "Provider failure must contain the complete error schema");
        require(error.at("source") == "sessions" && error.at("operation") == "request",
            "Request exception origin was lost");
        require(!event->references.empty() && event->references.front().type == "stream_id",
            "Stream reference was lost");
        try
        {
            std::rethrow_exception(failure);
        }
        catch (const std::filesystem::filesystem_error& original)
        {
            require(error.at("type") == "filesystem_error", "Filesystem type was lost");
            require(error.at("data").at(0).at("code") == original.code().value(), "Filesystem code was lost");
            require(error.at("data").at(0).at("path1") == "fixture/path", "Filesystem path was lost");
        }
        catch (const std::system_error& original)
        {
            require(error.at("type") == "system_error", "System type was lost");
            require(error.at("data").at(0).at("code") == original.code().value(), "System code was lost");
        }
        catch (const std::exception& original)
        {
            require(error.at("message") == original.what(), "Original message was changed");
        }
        catch (...)
        {
            require(error.at("type") == "unknown_exception" && error.at("message") == "",
                "Unknown exception must use the canonical schema without an invented message");
        }
    }
}

// Link the actual turn implementation to a request double so both catch branches
// can be exercised without credentials, provider access or network calls.
namespace sessions
{
    provider::CompactionResult request(
        const std::string&, provider::Provider, const std::string&, const std::string&,
        std::string_view, const nlohmann::json&, const nlohmann::json&, bool,
        const nlohmann::json&, provider::EventSink, provider::EventSink summary_sink,
        provider::CompactionResponse*)
    {
        if (finish_summary && summary_sink.on_finished != nullptr)
        {
            auto result = summary_sink.on_finished(summary_sink.context);
            require(!result.error, "Summary callback failed");
        }
        std::rethrow_exception(request_failure);
    }
}

int main(int argc, char** argv)
{
    try
    {
        print_wire = argc == 2 && std::string_view(argv[1]) == "--wire";
        for (const auto& failure : {
            std::make_exception_ptr(std::runtime_error("Original request exception")),
            std::make_exception_ptr(std::system_error(std::error_code(5, std::system_category()), "Native failure")),
            std::make_exception_ptr(std::filesystem::filesystem_error("Filesystem failure",
                std::filesystem::path("fixture/path"), std::make_error_code(std::errc::permission_denied))),
            std::make_exception_ptr(7)})
        {
            check_turn(failure, false, false);
            check_turn(failure, true, false);
            check_turn(failure, true, true);
        }
        if (!print_wire)
            std::cout << "sessions run_turn failure event tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
