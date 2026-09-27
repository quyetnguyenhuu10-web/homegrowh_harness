#include "ingress.h"
#include "channel.h"

#include <event_port>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace event_port::ipc
{
    namespace
    {
        constexpr const char* endpoint_environment = "HOMEGROWPH_EVENT_PORT";

        std::array<std::byte, 4> encode_u32(std::uint32_t value) noexcept
        {
            return {
                static_cast<std::byte>(value & 0xffu),
                static_cast<std::byte>((value >> 8) & 0xffu),
                static_cast<std::byte>((value >> 16) & 0xffu),
                static_cast<std::byte>((value >> 24) & 0xffu),
            };
        }

        std::uint32_t decode_u32(const std::array<std::byte, 4>& bytes) noexcept
        {
            return static_cast<std::uint32_t>(bytes[0])
                | (static_cast<std::uint32_t>(bytes[1]) << 8)
                | (static_cast<std::uint32_t>(bytes[2]) << 16)
                | (static_cast<std::uint32_t>(bytes[3]) << 24);
        }

        void read_exact(Connection& connection, std::span<std::byte> output)
        {
            std::size_t offset = 0;
            while (offset < output.size())
            {
                const std::size_t count = connection.read(output.subspan(offset));
                if (count == 0)
                    throw std::runtime_error("event_port external ingress frame ended early");
                offset += count;
            }
        }

        std::string read_frame(Connection& connection)
        {
            std::array<std::byte, 4> size_bytes{};
            read_exact(connection, size_bytes);
            const std::uint32_t size = decode_u32(size_bytes);
            std::string payload(size, '\0');
            if (size != 0)
            {
                read_exact(
                    connection,
                    std::as_writable_bytes(
                        std::span(payload.data(), payload.size())));
            }
            return payload;
        }

        void write_frame(Connection& connection, std::string_view payload)
        {
            if (payload.size() > (std::numeric_limits<std::uint32_t>::max)())
                throw std::length_error("event_port external response is too large");
            const auto size = encode_u32(static_cast<std::uint32_t>(payload.size()));
            connection.write(size);
            if (!payload.empty())
            {
                connection.write(std::as_bytes(
                    std::span(payload.data(), payload.size())));
            }
        }

        Level parse_level(const nlohmann::json& value)
        {
            if (!value.is_number_integer())
                throw std::invalid_argument("event_port external level must be an integer");
            const int level = value.get<int>();
            if (level < static_cast<int>(Level::trace)
                || level > static_cast<int>(Level::critical))
            {
                throw std::invalid_argument("event_port external level is out of range");
            }
            return static_cast<Level>(level);
        }

        References parse_references(const nlohmann::json& value)
        {
            if (!value.is_array())
                throw std::invalid_argument("event_port external references must be an array");
            References references;
            for (const nlohmann::json& item : value)
            {
                if (!item.is_object()
                    || !item.contains("type")
                    || !item.at("type").is_string()
                    || !item.contains("value")
                    || !item.at("value").is_string())
                {
                    throw std::invalid_argument("event_port external reference is invalid");
                }
                references.emplace_back(
                    item.at("type").get<std::string>(),
                    item.at("value").get<std::string>());
            }
            return references;
        }

        Emit parse_emit(std::string_view raw)
        {
            const nlohmann::json value = nlohmann::json::parse(raw);
            if (!value.is_object())
                throw std::invalid_argument("event_port external event must be an object");
            if (!value.contains("package") || !value.at("package").is_string())
                throw std::invalid_argument("event_port external package is missing");
            if (!value.contains("type") || !value.at("type").is_string())
                throw std::invalid_argument("event_port external type is missing");

            return Emit{
                value.at("package").get<std::string>(),
                value.contains("level")
                    ? parse_level(value.at("level"))
                    : Level::info,
                value.at("type").get<std::string>(),
                value.contains("references")
                    ? parse_references(value.at("references"))
                    : References{},
                value.contains("data") ? value.at("data") : nlohmann::json(nullptr),
            };
        }

        void publish_connection(Connection connection) noexcept
        {
            try
            {
                const std::string raw = read_frame(connection);
                (void)event_port::port(parse_emit(raw));
                write_frame(connection, R"({"ok":true})");
            }
            catch (const std::exception& error)
            {
                try
                {
                    write_frame(
                        connection,
                        nlohmann::json{
                            {"ok", false},
                            {"error", error.what()}
                        }.dump());
                }
                catch (...)
                {
                }
            }
        }

        void set_endpoint_environment(const std::string& endpoint)
        {
#if defined(_WIN32)
            if (_putenv_s(endpoint_environment, endpoint.c_str()) != 0)
                throw std::runtime_error("event_port failed to publish endpoint environment");
#else
            if (setenv(endpoint_environment, endpoint.c_str(), 1) != 0)
            {
                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "setenv(event_port endpoint)");
            }
#endif
        }

        class Ingress final
        {
        public:
            Ingress()
                : listener_()
            {
                set_endpoint_environment(listener_.endpoint());
                thread_ = std::jthread([this](std::stop_token stop) {
                    while (!stop.stop_requested())
                    {
                        try
                        {
                            std::optional<Connection> connection =
                                listener_.accept_for(std::chrono::milliseconds(100));
                            if (connection.has_value())
                                publish_connection(std::move(*connection));
                        }
                        catch (...)
                        {
                            if (stop.stop_requested())
                                return;
                        }
                    }
                });
            }

            Ingress(const Ingress&) = delete;
            Ingress& operator=(const Ingress&) = delete;

        private:
            Listener listener_;
            std::jthread thread_;
        };

        Ingress& ingress()
        {
            static Ingress value;
            return value;
        }
    }

    void ensure_ingress()
    {
        static_cast<void>(ingress());
    }
}
