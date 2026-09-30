#include "test_support.h"

#include <iostream>
#include <system_error>
#include <thread>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <cerrno>
#endif

namespace
{
    using namespace ipc_test_support;

    void validation_errors()
    {
        for (const std::string& name : {std::string{}, std::string("bad/name"),
                std::string("bad\\name"), std::string("bad\0name", 8)})
        {
            const auto listener = ipc::listen(name);
            const auto& listen_error = require_error(listener.error, "listen", "validation_error");
            require(listen_error.data.at(0).at("name") == name, "Invalid endpoint name was lost");
            const auto client = ipc::connect(name);
            require_error(client.error, "connect", "validation_error");
        }
        ipc::server empty_listener;
        const auto accepted = ipc::accept(empty_listener);
        require_error(accepted.error, "accept", "validation_error");
        ipc::connection empty_connection;
        const auto read = ipc::read(empty_connection);
        require_error(read.error, "read", "validation_error");
        require(!read.closed && read.data.empty(), "Read error was mistaken for data or EOF");
        const auto write = ipc::write(empty_connection, {});
        require_error(write.error, "write", "validation_error");

        auto pair = open_endpoints(endpoint_name());
        const std::vector<std::uint8_t> oversized(16 * 1024 * 1024 + 1);
        const auto rejected = ipc::write(pair.client, oversized);
        const auto& error = require_error(rejected.error, "write", "validation_error");
        require(error.data.at(0).at("size") == oversized.size(), "Rejected payload size was lost");
        require(!error.data.at(0).contains("code"), "Validation invented an OS error code");
        const std::vector<std::uint8_t> valid{0x00, 0xff};
        require(!ipc::write(pair.client, valid).error, "Rejected write damaged the connection");
        require(ipc::read(pair.accepted).data == valid, "Rejected write emitted a frame");
    }

    void native_errors()
    {
        const std::string name = endpoint_name();
        auto missing = ipc::connect(name);
        const auto& error = require_error(missing.error, "connect", "system_error");
        const auto& data = error.data.at(0);
#ifdef _WIN32
        const int code = ERROR_FILE_NOT_FOUND;
        const auto& category = std::system_category();
        require(data.at("api") == "CreateFileW", "Missing pipe API was lost");
        require(data.at("path") == "\\\\.\\pipe\\" + name, "Missing pipe path was lost");
#else
        const int code = ENOENT;
        const auto& category = std::generic_category();
        require(data.at("api") == "connect", "Missing socket API was lost");
        require(data.at("path") == "/tmp/" + name + ".sock", "Missing socket path was lost");
#endif
        require(data.at("code") == code, "Original native error code was changed");
        require(data.at("category") == category.name(), "Native error category was lost");
        require(error.message == std::error_code(code, category).message(), "Original OS message was changed");
        require(error.causes.empty(), "A forwarded native error was unnecessarily wrapped");
        ipc::connection failed_value = std::move(missing.value);
        require_error(ipc::read(failed_value).error, "read", "validation_error");

        auto listener = ipc::listen(name);
        require(!listener.error, "Listener failed");
        const auto duplicate = ipc::listen(name);
        const auto& duplicate_error = require_error(duplicate.error, "listen", "system_error");
        require(duplicate_error.data.at(0).at("code").get<int>() != 0, "Duplicate listener lost its OS code");
        require(duplicate_error.data.at(0).contains("path"), "Duplicate listener lost its path");
        auto client = ipc::connect(name);
        require(!client.error, "Failed duplicate listener damaged the original endpoint");
        auto accepted = ipc::accept(listener.value);
        require(!accepted.error, "Original listener could not accept after duplicate failure");
        client.value = ipc::connection{};
        const auto broken_write = ipc::write(accepted.value, std::vector<std::uint8_t>{1});
        const auto& broken = require_error(broken_write.error, "write", "system_error");
        require(broken.data.at(0).contains("api") && broken.data.at(0).contains("path"),
            "Write failure lost native API or endpoint");
        require(broken.data.at(0).at("phase") == "header", "Write failure lost frame context");
        require(broken.data.at(0).at("transferred") == 0, "Write failure lost byte progress");
        const int broken_code = broken.data.at(0).at("code").get<int>();
        require(broken.message == std::error_code(broken_code, category).message(), "Broken pipe message was changed");

#ifdef _WIN32
        const std::string invalid_utf8("\xc3\x28", 2);
        const auto invalid = ipc::connect(invalid_utf8);
        const auto& utf8 = require_error(invalid.error, "connect", "system_error");
        require(utf8.data.at(0).at("code") == ERROR_NO_UNICODE_TRANSLATION, "UTF-8 error code was changed");
        require(utf8.data.at(0).at("api") == "MultiByteToWideChar", "UTF-8 API was lost");
        require(utf8.data.at(0).at("name_bytes") == nlohmann::json::array({0xc3, 0x28}), "Invalid UTF-8 bytes were lost");
        require(!nlohmann::json(utf8).dump().empty(), "UTF-8 error cannot be serialized as JSON");
#else
        const auto long_path = ipc::listen(std::string(200, 'x'));
        const auto& path_error = require_error(long_path.error, "listen", "validation_error");
        require(path_error.data.at(0).contains("path"), "Long socket path was lost");
#endif
    }

    void error_payload_roundtrip()
    {
        auto pair = open_endpoints(endpoint_name());
        const auto native = ipc::connect(endpoint_name());
        require(native.error.has_value(), "Roundtrip needs a native error");
        const ipc::Error sibling{"plugin_loader", "invoke", "protocol_error", "Schema mismatch",
            {nullptr, true, 7, 1.5, "text", nlohmann::json::array({1, "two"}),
             nlohmann::json{{"plugin_id", "test"}, {"schema_errors", nlohmann::json::array({"x"})}}}, {}};
        const ipc::Error nested{"sessions", "run_request", "dependency_error", "IPC request failed",
            {}, {*native.error}};
        const ipc::Error combined{"sessions", "run_tool", "dependency_error", "Dependencies failed",
            {nlohmann::json{{"body", nlohmann::json{{"structured", true}}}, {"path", "test"}}},
            {nested, sibling}};
        require_schema(combined);
        const nlohmann::json event = {
            {"package", "sessions"}, {"level", "error"}, {"type", "command_failed"},
            {"references", nlohmann::json::array()}, {"data", {{"error", combined}}},
        };
        const auto wire = nlohmann::json::to_cbor(event);
        require(!ipc::write(pair.client, wire).error, "Structured error send failed");
        const auto received = ipc::read(pair.accepted);
        require(!received.error && !received.closed, "Structured error receive failed");
        const auto decoded = nlohmann::json::from_cbor(received.data);
        require(decoded == event, "IPC changed error identity, payload, or causes");
        require(decoded.at("data").at("error").at("causes").size() == 2, "Independent causes were flattened");
        const auto& child = decoded.at("data").at("error").at("causes").at(0).at("causes").at(0);
        require(child == nlohmann::json(*native.error), "Wrapped native error lost information");
        const ipc::Error empty{"ipc", "read", "protocol_error", "Empty arrays", {}, {}};
        require_schema(empty);
        require(nlohmann::json(empty).at("data").empty(), "Empty data is not an array");
    }

    void large_payloads_and_cleanup()
    {
        const auto name = endpoint_name();
        {
            auto pair = open_endpoints(name);
            const std::vector<std::uint8_t> payload(1024 * 1024, 0xa5);
            ipc::write_result client_write;
            ipc::write_result server_write;
            std::thread client_writer([&] { client_write = ipc::write(pair.client, payload); });
            const auto at_server = ipc::read(pair.accepted);
            client_writer.join();
            std::thread server_writer([&] { server_write = ipc::write(pair.accepted, payload); });
            const auto at_client = ipc::read(pair.client);
            server_writer.join();
            require(!client_write.error && !server_write.error, "Large payload writes failed");
            require(!at_server.error && at_server.data == payload, "Large server read was corrupted");
            require(!at_client.error && at_client.data == payload, "Large client read was corrupted");
            pair.accepted = ipc::connection{};
            const auto eof = ipc::read(pair.client);
            require(!eof.error && eof.closed && eof.data.empty(), "Clean EOF was mistaken for failure");
        }
        auto reused = ipc::listen(name);
        require(!reused.error, "RAII cleanup left an endpoint or handle behind");
    }
}

int main()
{
    try
    {
        validation_errors();
        native_errors();
        error_payload_roundtrip();
        large_payloads_and_cleanup();
        std::cout << "ipc schema and native error tests passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
