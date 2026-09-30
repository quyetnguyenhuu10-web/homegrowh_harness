#include <session>
#include <event_port>
#include <request/request.h>
#include <request/turn.h>
#include <session/credential_owner.h>
#include <error/error.h>
#include <error/event_port.h>
#include "wire_json.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace
{
    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
}

int main()
{
    try
    {
        const auto native = secrets::resolve_secure_session("");
        require(native.error.has_value(), "Expected a credential validation error");
        std::exception_ptr failure;
        try
        {
            (void)sessions::request("", provider::Provider::openai, "", "", "",
                nlohmann::json::object(), nlohmann::json::array(), false);
        }
        catch (const sessions::ErrorException& exception)
        {
            require(exception.error().source == "sessions", "Session context was not added");
            require(exception.error().operation == "request", "Session operation was not preserved");
            require(exception.error().causes.size() == 1, "Credential cause was not retained");
            require(nlohmann::json(exception.error().causes.front()) == nlohmann::json(*native.error),
                "Session changed the original credential error");
            failure = std::current_exception();
        }
        require(failure != nullptr, "Session did not return the structured credential failure");
        const auto wire = sessions_runtime::error_json(failure);
        require(wire.at("data").is_array() && wire.at("causes").is_array(),
            "IPC did not preserve array payloads");
        require(wire.at("causes").at(0) == nlohmann::json(*native.error),
            "IPC flattened the credential cause");

        auto registration = sessions::detail::checked_port(event_port::Register{"sessions", {}});
        sessions::detail::emit_session_failure({failure, sessions::SessionState::request});
        const auto event = sessions::detail::checked_port(event_port::Read{registration});
        require(event->data.at("error") == wire, "EventPort did not preserve the complete Error");
        require(!event->data.contains("raw"), "Structured error was also flattened to raw text");

        const auto invalid_emit = event_port::port(event_port::Emit{"", event_port::Level::error, "", {}, {}});
        require(invalid_emit.error.has_value(), "Expected EventPort validation failure");
        try
        {
            sessions::detail::checked_port(event_port::Emit{"", event_port::Level::error, "", {}, {}});
            require(false, "Session adapter discarded the EventPort failure");
        }
        catch (const sessions::ErrorException& exception)
        {
            require(nlohmann::json(exception.error()) == nlohmann::json(*invalid_emit.error),
                "Session adapter changed the EventPort identity, payload or causes");
            require(sessions_runtime::error_json(std::current_exception()) == nlohmann::json(*invalid_emit.error),
                "IPC changed the EventPort error schema");
        }

        std::string empty;
        const auto stored = sessions::detail::persist_session_credential(empty);
        require(stored.error && !stored.value, "Credential owner must return a Result failure");
        require(stored.error->data.front().at("field") == "raw_api_key",
            "Credential owner lost validation context");

        const auto invalid = secrets::set("", "");
        sessions::Error grouped = *invalid.error;
        const auto grouped_wire = sessions_runtime::error_json(std::make_exception_ptr(
            sessions::ErrorException(std::move(grouped))));
        require(grouped_wire == nlohmann::json(*invalid.error),
            "IPC lost independent causes or changed the envelope");

        provider::Error provider_failure{
            "provider", "compaction", "dependency_error", "Provider failure",
            {{{"model", "fixture"}}}, {}};
        provider_failure.causes.push_back(provider::Error{
            "provider", "request", "http_error", "HTTP 429",
            {{{"status_code", 429}, {"body", { {"retry_after", 3} }}}}, {}});
        const auto original_provider = provider::serialize_error(provider_failure);
        require(original_provider.value && !original_provider.error,
            "Provider fixture could not be serialized");
        const auto imported_provider = sessions::detail::convert_error<sessions::Error>(
            std::move(provider_failure));
        require(nlohmann::json(imported_provider) == *original_provider.value,
            "Session adapter changed provider identity, payload or causes");

        sessions::detail::CredentialOwner invalid_owner(std::string("prefix\0suffix", 13));
        invalid_owner.close_after_primary_error();
        const auto* secondary = invalid_owner.secondary_cleanup_error();
        require(secondary != nullptr && secondary->causes.size() == 1,
            "Secondary credential cleanup lost the native cause");
        require(nlohmann::json(secondary->causes.front()) ==
            nlohmann::json(*secrets::erase_session(std::string("prefix\0suffix", 13)).error),
            "Secondary credential cleanup changed the original failure");
        std::cout << "session credential propagation, EventPort and IPC tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
