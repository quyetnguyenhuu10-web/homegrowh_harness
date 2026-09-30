#include <session>
#include <event_port>
#include <error/error.h>
#include <error/event_port.h>
#include <request/turn.h>
#include "ipc_failure.h"
#include "runtime.h"
#include "wire_json.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    using nlohmann::json;
    bool print_wire = false;

    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void require_error(const json& error)
    {
        require(error.is_object() && error.size() == 6, "Error must have exactly six fields");
        for (const char* key : {"source", "operation", "type", "message"})
            require(error.at(key).is_string(), "Error text field must be a string");
        require(error.at("data").is_array(), "Error data must be an array");
        require(error.at("causes").is_array(), "Error causes must be an array");
        for (const auto& cause : error.at("causes"))
            require_error(cause);
    }

    event_port::EventPtr read_error(event_port::Registration& registration)
    {
        auto event = sessions::detail::checked_port(event_port::Read{registration});
        require(event != nullptr, "Missing emitted error event");
        require(event->level >= event_port::Level::error, "Expected error event level");
        require(!event->data.contains("raw"), "Error event still contains legacy raw");
        require_error(event->data.at("error"));
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
        return event;
    }

    std::exception_ptr nested_failure(const std::exception_ptr& cause)
    {
        try
        {
            std::rethrow_exception(cause);
        }
        catch (...)
        {
            try
            {
                std::throw_with_nested(std::runtime_error("Outer context"));
            }
            catch (...)
            {
                return std::current_exception();
            }
        }
    }

    sessions::Error original_error()
    {
        return {"sandbox", "invoke", "dependency_error", "Original failure",
            {nullptr, 7, "text", json::array({true, false}), json{{"path", "fixture/path"}}},
            {{"provider", "request", "http_error", "HTTP 429",
                {json{{"status_code", 429}, {"body", json{{"retry_after", 3}}}}}, {}},
             {"plugin_loader", "load", "protocol_error", "Invalid manifest",
                {json{{"plugin_id", "fixture"}, {"schema_errors", json::array({"missing api"})}}}, {}}}};
    }

    void check_serialization()
    {
        const std::system_error system(std::error_code(5, std::system_category()), "Native failure");
        const auto system_wire = sessions_runtime::error_json(std::make_exception_ptr(system), "native_call");
        require_error(system_wire);
        require(system_wire.at("source") == "session_runtime", "Runtime origin was lost");
        require(system_wire.at("operation") == "native_call", "Runtime operation was lost");
        require(system_wire.at("type") == "system_error", "System error type was lost");
        require(system_wire.at("message") == system.what(), "Original native message was changed");
        require(system_wire.at("data").at(0).at("code") == system.code().value(), "Native code was changed");
        require(system_wire.at("data").at(0).at("category") == system.code().category().name(),
            "Native category was changed");

        const std::string path1_utf8 = "folder/\xc4\x91\xc6\xb0\xe1\xbb\x9dng";
        const std::filesystem::filesystem_error filesystem("Filesystem failure",
            std::filesystem::u8path(path1_utf8), std::filesystem::path("destination"), system.code());
        const auto filesystem_wire = sessions_runtime::error_json(std::make_exception_ptr(filesystem));
        require_error(filesystem_wire);
        require(filesystem_wire.at("type") == "filesystem_error", "Filesystem error type was lost");
        const auto& details = filesystem_wire.at("data").at(0);
        require(details.at("path1") == path1_utf8 && details.at("path2") == "destination",
            "Filesystem paths must be preserved as UTF-8");
        require(details.at("code") == system.code().value(), "Filesystem code was lost");

        const auto original = original_error();
        const auto structured = std::make_exception_ptr(sessions::ErrorException(sessions::Error(original)));
        require(sessions_runtime::error_json(structured, "outer_operation") == json(original),
            "Structured error identity, payload or independent causes were changed");
        require(json(sessions::detail::exception_error("outer_operation", structured)) == json(original),
            "Session adapter changed an existing structured error");

        auto ipc_original = sessions::detail::convert_error<ipc::Error>(sessions::Error(original));
        ipc_original.source = "ipc";
        const auto ipc_wire = json(ipc_original);
        const auto ipc = std::make_exception_ptr(sessions_runtime::IpcFailure(std::move(ipc_original)));
        require(sessions_runtime::error_json(ipc, "outer_operation") == ipc_wire,
            "IPC error identity, payload or causes were changed");
        require(sessions_runtime::error_json(nested_failure(ipc)).at("causes").at(0) == ipc_wire,
            "Nested IPC failure was flattened");
        require(sessions_runtime::error_json(nested_failure(structured)).at("causes").at(0) == json(original),
            "Nested structured failure was flattened");
        require(sessions_runtime::error_json(nested_failure(std::make_exception_ptr(system)), "native_call")
            .at("causes").at(0) == system_wire, "Nested system failure was flattened");
        require(sessions_runtime::error_json(nullptr).at("type") == "missing_exception",
            "Missing exception was not represented explicitly");
        require(sessions_runtime::error_json(std::make_exception_ptr(7)).at("type") == "unknown_exception",
            "Non-standard exception was not represented explicitly");
    }

    void check_events()
    {
        std::vector<std::exception_ptr> failures{
            std::make_exception_ptr(std::invalid_argument("Invalid argument")),
            std::make_exception_ptr(std::logic_error("Invalid state")),
            std::make_exception_ptr(std::runtime_error("Runtime failure")),
            std::make_exception_ptr(std::system_error(std::error_code(5, std::system_category()), "Native failure")),
            std::make_exception_ptr(std::filesystem::filesystem_error("Filesystem failure",
                std::filesystem::path("source"), std::filesystem::path("destination"),
                std::make_error_code(std::errc::permission_denied))),
            std::make_exception_ptr(std::filesystem::filesystem_error("Unicode filesystem failure",
                std::filesystem::path(u8"folder/\u0111\u01b0\u1eddng"),
                std::make_error_code(std::errc::permission_denied))),
            std::make_exception_ptr(sessions::ErrorException(original_error())),
            nested_failure(std::make_exception_ptr(sessions::ErrorException(original_error()))),
            std::make_exception_ptr(7), nullptr};
        auto session_events = sessions::detail::checked_port(event_port::Register{"sessions", {}});
        auto runtime_events = sessions::detail::checked_port(event_port::Register{"session_runtime", {}});
        sessions_runtime::Runtime runtime;
        for (const auto& failure : failures)
        {
            sessions::detail::emit_session_failure({failure, sessions::SessionState::request});
            const auto session_event = read_error(session_events);
            require(session_event->type == "failed" && session_event->data.at("phase") == "request",
                "Session event context was changed");
            require(session_event->data.at("error") == json(sessions::detail::exception_error("request", failure)),
                "Session event lost error details");

            runtime.emit_protocol_error(failure);
            const auto protocol = read_error(runtime_events);
            require(protocol->type == "protocol_error", "Protocol event kind was changed");
            require(protocol->data.at("error") == sessions_runtime::error_json(failure, "decode_command"),
                "Protocol event lost error details or operation");

            runtime.emit_runtime_failure("startup", failure, 3);
            const auto failed = read_error(runtime_events);
            require(failed->type == "runtime_failed" && failed->data.at("exit_code") == 3,
                "Runtime lifecycle context was changed");
            require(failed->data.at("error") == sessions_runtime::error_json(failure, "startup"),
                "Runtime event lost error details or operation");
        }

        runtime.emit_protocol_error(std::make_exception_ptr(sessions_runtime::IpcFailure(
            sessions::detail::convert_error<ipc::Error>(original_error()))));
        require(read_error(runtime_events)->data.at("error") == json(original_error()),
            "Runtime event changed a structured IPC failure");

        try
        {
            (void)sessions_runtime::decode_command({0xff});
            require(false, "Malformed CBOR command must fail");
        }
        catch (const json::parse_error& exception)
        {
            runtime.emit_protocol_error(std::current_exception());
            const auto protocol = read_error(runtime_events);
            const auto& error = protocol->data.at("error");
            require(error.at("type") == "json_error", "CBOR parse error type was lost");
            require(error.at("data").at(0).at("id") == exception.id &&
                error.at("data").at(0).at("byte") == exception.byte,
                "CBOR parse error id or offset was lost");
        }

        require(!runtime.execute({1, sessions_runtime::CommandOpcode::register_session, json::object()}),
            "Invalid registration must not close the runtime");
        const auto invalid = read_error(runtime_events);
        require(invalid->type == "command_failed" && invalid->data.at("command_id") == 1,
            "Command failure metadata was lost");
        require(invalid->data.at("error").at("operation") == "register_session",
            "Registration error lost the operation");

        require(!runtime.execute({2, sessions_runtime::CommandOpcode::declare_request, nullptr}),
            "Missing session must not close the runtime");
        const auto logic = read_error(runtime_events);
        require(logic->data.at("error").at("message") == "session is not registered",
            "Logic exception message was lost");
        require(logic->data.at("error").at("operation") == "declare_request",
            "Command error operation was lost");

        json config = {{"api_key_raw", "fixture"}, {"history", json::array()},
            {"session_current", json{{"messages", json::array()}}}, {"tool_definitions", json::array()},
            {"provider", "openai"}, {"endpoint", "unused"}, {"model_id", "fixture"},
            {"context_limit", 10}, {"compact_threshold", 8}, {"tool_result_timeout_ms", -1},
            {"session_timeout_ms", -1}, {"compaction_prompt", ""}, {"workspace_path", "."},
            {"tool_runtime_executable", "missing-error-boundary-fixture/tool.exe"},
            {"sandbox_config", {{"read_only", json::array()}, {"read_write", json::array()}, {"network", "none"}}},
            {"refresh_workspace", false}};
        require(!runtime.execute({3, sessions_runtime::CommandOpcode::register_session, std::move(config)}),
            "Filesystem registration failure must not close the runtime");
        const auto filesystem = read_error(runtime_events);
        require(filesystem->data.at("error").at("type") == "filesystem_error",
            "Registration filesystem failure did not reach the boundary");
        require(filesystem->data.at("error").at("data").at(0).at("path1") ==
            "missing-error-boundary-fixture/tool.exe", "Registration filesystem path was lost");
    }
}

int main(int argc, char** argv)
{
    try
    {
        print_wire = argc == 2 && std::string_view(argv[1]) == "--wire";
        check_serialization();
        check_events();
        if (!print_wire)
            std::cout << "sessions exception, EventPort and runtime boundary tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
