#pragma once

#include <cstddef>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace event_port::ipc
{
    namespace detail
    {
        class connection_backend;
        class listener_backend;
    }

    class Connection final
    {
    public:
        Connection(Connection&&) noexcept;
        Connection& operator=(Connection&&) noexcept;
        ~Connection();

        Connection(const Connection&) = delete;
        Connection& operator=(const Connection&) = delete;

        /**
         * Read up to buffer.size() raw bytes.
         * Returns zero for an empty buffer or after the peer has closed the stream.
         */
        std::size_t read(std::span<std::byte> buffer);

        /** Write the complete raw byte sequence or throw the native OS error. */
        void write(std::span<const std::byte> data);

    private:
        explicit Connection(std::unique_ptr<detail::connection_backend>&& backend) noexcept;

        std::unique_ptr<detail::connection_backend> backend_;

        friend class Listener;
        friend Connection connect(const std::string& endpoint);
    };

    class Listener final
    {
    public:
        Listener();
        Listener(Listener&&) noexcept;
        Listener& operator=(Listener&&) noexcept;
        ~Listener();

        Listener(const Listener&) = delete;
        Listener& operator=(const Listener&) = delete;

        /**
         * Opaque OS endpoint for the peer.
         * Windows: named-pipe name. Linux: abstract AF_UNIX name prefixed by '@'.
         */
        [[nodiscard]] const std::string& endpoint() const noexcept;

        /** Block until one peer connects, then transfer that connection out. */
        Connection accept();

        /** Return no connection when the timeout expires before a peer connects. */
        std::optional<Connection> accept_for(std::chrono::milliseconds timeout);

    private:
        std::unique_ptr<detail::listener_backend> backend_;
    };

    /** Connect to an opaque endpoint produced by Listener::endpoint(). */
    Connection connect(const std::string& endpoint);
}
