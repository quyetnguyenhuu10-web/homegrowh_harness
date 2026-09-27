#pragma once

#include <cstddef>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace event_port::ipc::detail
{
    class connection_backend
    {
    public:
        virtual ~connection_backend() = default;

        connection_backend(const connection_backend&) = delete;
        connection_backend& operator=(const connection_backend&) = delete;

        virtual std::size_t read(std::span<std::byte> buffer) = 0;
        virtual void write(std::span<const std::byte> data) = 0;

    protected:
        connection_backend() = default;
    };

    class listener_backend
    {
    public:
        virtual ~listener_backend() = default;

        listener_backend(const listener_backend&) = delete;
        listener_backend& operator=(const listener_backend&) = delete;

        [[nodiscard]] virtual const std::string& endpoint() const noexcept = 0;
        virtual std::unique_ptr<connection_backend> accept() = 0;
        virtual std::unique_ptr<connection_backend> accept_for(
            std::chrono::milliseconds timeout) = 0;

    protected:
        listener_backend() = default;
    };

    std::unique_ptr<listener_backend> make_listener();
    std::unique_ptr<connection_backend> connect(const std::string& endpoint);
}
