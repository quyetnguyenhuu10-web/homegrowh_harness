#pragma once

#include <ipc>

#include <chrono>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace ipc_test_support
{
    inline void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    inline std::string endpoint_name()
    {
        return "ipc_schema_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    inline void require_schema(const ipc::Error& error)
    {
        const nlohmann::json wire = error;
        require(wire.is_object() && wire.size() == 6, "Error envelope must have exactly six fields");
        for (const char* field : {"source", "operation", "type", "message"})
            require(wire.at(field).is_string(), "Error text field must be a string");
        require(wire.at("data").is_array(), "Error data must be an array");
        require(wire.at("causes").is_array(), "Error causes must be an array");
        for (const ipc::Error& cause : error.causes)
            require_schema(cause);
    }

    inline const ipc::Error& require_error(
        const std::optional<ipc::Error>& error,
        std::string_view operation, std::string_view type)
    {
        require(error.has_value(), "Expected a structured IPC error");
        require_schema(*error);
        require(error->source == "ipc", "IPC error source was changed");
        require(error->operation == operation, "IPC error operation was changed");
        require(error->type == type, "IPC error type was changed");
        return *error;
    }

    struct endpoints
    {
        ipc::server listener;
        ipc::connection client;
        ipc::connection accepted;
    };

    inline endpoints open_endpoints(const std::string& name)
    {
        auto listener = ipc::listen(name);
        require(!listener.error, "Test listener failed");
        auto client = ipc::connect(name);
        require(!client.error, "Test client failed");
        auto accepted = ipc::accept(listener.value);
        require(!accepted.error, "Test accept failed");
        return {std::move(listener.value), std::move(client.value), std::move(accepted.value)};
    }
}
