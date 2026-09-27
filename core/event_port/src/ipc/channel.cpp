#include "channel.h"
#include "platform.h"

#include <stdexcept>
#include <utility>

namespace event_port::ipc
{
    Connection::Connection(std::unique_ptr<detail::connection_backend>&& backend) noexcept
        : backend_(std::move(backend))
    {
    }

    Connection::Connection(Connection&&) noexcept = default;
    Connection& Connection::operator=(Connection&&) noexcept = default;
    Connection::~Connection() = default;

    std::size_t Connection::read(std::span<std::byte> buffer)
    {
        if (!backend_)
            throw std::logic_error("event_port ipc connection has no backend");
        return backend_->read(buffer);
    }

    void Connection::write(std::span<const std::byte> data)
    {
        if (!backend_)
            throw std::logic_error("event_port ipc connection has no backend");
        backend_->write(data);
    }

    Listener::Listener()
        : backend_(detail::make_listener())
    {
    }

    Listener::Listener(Listener&&) noexcept = default;
    Listener& Listener::operator=(Listener&&) noexcept = default;
    Listener::~Listener() = default;

    const std::string& Listener::endpoint() const noexcept
    {
        return backend_->endpoint();
    }

    Connection Listener::accept()
    {
        return Connection(backend_->accept());
    }

    std::optional<Connection> Listener::accept_for(std::chrono::milliseconds timeout)
    {
        if (timeout.count() < 0)
            throw std::invalid_argument("event_port ipc accept timeout must not be negative");
        std::unique_ptr<detail::connection_backend> backend =
            backend_->accept_for(timeout);
        if (!backend)
            return std::nullopt;
        return Connection(std::move(backend));
    }

    Connection connect(const std::string& endpoint)
    {
        if (endpoint.empty())
            throw std::invalid_argument("event_port ipc endpoint must not be empty");
        return Connection(detail::connect(endpoint));
    }
}
