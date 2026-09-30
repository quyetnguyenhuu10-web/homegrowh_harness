#include <event_port>
#include <event_port/detail/error.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    void require_schema(const nlohmann::json& error)
    {
        require(error.is_object() && error.size() == 6, "Error must have exactly six fields");
        for (const char* field : {"source", "operation", "type", "message"})
            require(error.at(field).is_string(), "Error identity fields must be strings");
        require(error.at("data").is_array(), "Error data must be an array");
        require(error.at("causes").is_array(), "Error causes must be an array");
        for (const auto& cause : error.at("causes"))
            require_schema(cause);
    }

    void validation_errors()
    {
        event_port::References references;
        references.emplace_back(std::string{}, std::string("first"));
        references.emplace_back(std::string{}, std::string("second"));
        const auto invalid = event_port::port(event_port::Emit{
            "", event_port::Level::error, "", std::move(references), nullptr});
        require(!invalid && !invalid.value && invalid.error, "Invalid Emit must return Result.error");
        const auto wire = nlohmann::json(*invalid.error);
        require_schema(wire);
        require(wire.at("operation") == "emit", "Emit context was lost");
        const auto& validation = wire.at("causes").at(0);
        require(validation.at("causes").size() == 3, "Independent validation failures were lost");
        require(validation.at("causes").at(0).at("data").at(0).at("field") == "package",
            "Package validation context was lost");
        require(validation.at("causes").at(1).at("data").at(0).at("field") == "type",
            "Type validation context was lost");
        const auto& reference_errors = validation.at("causes").at(2).at("causes");
        require(reference_errors.size() == 2, "Independent reference errors were flattened");
        require(reference_errors.at(0).at("data").at(0).at("index") == 0 &&
            reference_errors.at(1).at("data").at(0).at("value") == "second",
            "Reference index or payload was lost");

        event_port::References invalid_filter;
        invalid_filter.emplace_back(std::string{}, std::string("filter"));
        const auto registration = event_port::port(event_port::Register{"", std::move(invalid_filter)});
        require(!registration && !registration.value && registration.error,
            "Invalid Register must return Result.error");
        require(registration.error->operation == "register" && registration.error->causes.size() == 1,
            "Register context or lower validation error was lost");
        require_schema(nlohmann::json(*registration.error));
    }

    void registration_states()
    {
        auto registered = event_port::port(event_port::Register{"fixture", {}});
        require(registered.value && !registered.error, "Register success branch is invalid");
        auto registration = std::move(*registered.value);
        const auto read_moved = event_port::port(event_port::Read{*registered.value});
        require(!read_moved && read_moved.error->type == "invalid_state" &&
            read_moved.error->data.front().at("state") == "moved_from",
            "Moved-from Read must return the original state in Error.data");
        const auto close_moved = event_port::port(event_port::Close{*registered.value});
        require(!close_moved && !close_moved.value && close_moved.error->operation == "close",
            "Moved-from Close must return Result.error");
        require_schema(nlohmann::json(*close_moved.error));

        const auto closed = event_port::port(event_port::Close{registration});
        require(closed.value && !closed.error, "Close success branch is invalid");
        const auto read_closed = event_port::port(event_port::Read{registration});
        require(!read_closed && !read_closed.value && read_closed.error->type == "registration_closed",
            "Closed Read must return a semantic closure error");
        const auto closed_again = event_port::port(event_port::Close{registration});
        require(closed_again.value && !closed_again.error, "Close must remain idempotent");
        require(registration.cleanup_error() == nullptr, "Successful cleanup reported an error");
    }

    void event_error_payload()
    {
        event_port::Error original{
            "plugin", "invoke", "dependency_error", "Plugin invocation failed",
            {nlohmann::json{{"plugin_id", "fixture"}}, 42, "payload", nullptr, nlohmann::json::array({1, 2})},
            {event_port::Error{
                "sandbox", "grant_filesystem", "system_error", "Native API failed",
                {{{"code", 5}, {"category", "system"}, {"api", "fixture_api"}, {"path", "fixture/path"}}}, {}}}};
        const nlohmann::json expected = original;
        require_schema(expected);
        auto registered = event_port::port(event_port::Register{"fixture", {}});
        require(registered.value && !registered.error, "Payload registration failed");
        auto registration = std::move(*registered.value);
        const auto emitted = event_port::port(event_port::Emit{
            "fixture", event_port::Level::error, "command_failed", {},
            {{"error", original}, {"context", { {"command", "fixture"} }}}});
        require(emitted.value && !emitted.error, "Structured error event emission failed");
        require((*emitted.value)->sequence == 0, "Rejected events must not consume a sequence");
        const auto read = event_port::port(event_port::Read{registration});
        require(read.value && !read.error, "Structured error event read failed");
        require(read.value->get() == emitted.value->get(), "Event was copied");
        require((*read.value)->data.at("error") == expected,
            "EventPort changed error identity, native payload or causes");
        require((*read.value)->data.at("context").at("command") == "fixture",
            "Event envelope context was lost");

        const auto open_payload = event_port::port(event_port::Emit{
            "fixture", event_port::Level::info, "content", {}, nlohmann::json::array({"arbitrary", 3})});
        require(open_payload.value && !open_payload.error && (*open_payload.value)->data.is_array(),
            "Generic event payload must remain open");
        const auto drained = event_port::port(event_port::Read{registration});
        require(drained.value && !drained.error && (*drained.value)->data == (*open_payload.value)->data,
            "Generic event payload changed during delivery");
    }

    void native_exception_payload()
    {
        const std::system_error native(std::make_error_code(std::errc::permission_denied), "fixture lock");
        auto failed = event_port::detail::guard<void>("wait_for_event", [&]() -> event_port::Result<void>
        {
            throw native;
        });
        require(!failed.value && failed.error, "Native failure must return Result.error");
        require(failed.error->type == "system_error" && failed.error->message == native.what(),
            "Native error type or message changed");
        const auto& payload = failed.error->data.front();
        require(payload.at("code") == native.code().value() &&
            payload.at("category") == native.code().category().name(),
            "Native error code or category changed");
        const auto original = nlohmann::json(*failed.error);
        const auto forwarded = event_port::detail::guard<void>("outer", [&]() -> event_port::Result<void>
        {
            return event_port::Result<void>::failure(std::move(*failed.error));
        });
        require(nlohmann::json(*forwarded.error) == original, "Guard must forward an existing Error unchanged");
        event_port::Error cause = *forwarded.error;
        const auto wrapped = event_port::detail::context_error(
            "emit", "dependency_error", "Fixture emission failed", {}, std::move(cause));
        require_schema(nlohmann::json(wrapped));
        require(nlohmann::json(wrapped.causes.front()) == original, "Context wrapper lost the native cause");

        const auto unknown = event_port::detail::guard<void>("fixture", []() -> event_port::Result<void>
        {
            throw 1;
        });
        require(unknown.error && !unknown.value && unknown.error->type == "unknown_exception",
            "Unknown exception escaped the Result boundary");
        require_schema(nlohmann::json(*unknown.error));
    }
}

int main()
{
    validation_errors();
    registration_states();
    event_error_payload();
    native_exception_payload();
    std::cout << "event_port error schema tests passed\n";
    return EXIT_SUCCESS;
}
