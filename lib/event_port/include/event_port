#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include <nlohmann/json.hpp>

namespace event_port
{
    enum class Level
    {
        trace,
        debug,
        info,
        warning,
        error,
        critical
    };

    struct Reference
    {
        std::string type;
        std::string value;

        Reference(std::string&& type, std::string&& value) noexcept;

        Reference(const Reference&) = delete;
        Reference& operator=(const Reference&) = delete;
        Reference(Reference&&) noexcept = default;
        Reference& operator=(Reference&&) noexcept = default;
    };

    using References = std::deque<Reference>;

    struct Event;
    using EventPtr = std::shared_ptr<const Event>;

    class Registration;
    struct Register;
    struct Read;
    struct Emit;
    struct Close;

    template <typename Operation>
    auto port(Operation&& operation);

    namespace detail
    {
        struct RegistrationState;
        struct EventAccess;
        class PortDispatch;

        template <typename>
        inline constexpr bool unsupported_operation = false;
    }

    class Registration
    {
    public:
        ~Registration();

        Registration(const Registration&) = delete;
        Registration& operator=(const Registration&) = delete;
        Registration(Registration&&) noexcept;
        Registration& operator=(Registration&&) noexcept;

    private:
        explicit Registration(
            std::shared_ptr<detail::RegistrationState>&& state) noexcept;

        std::shared_ptr<detail::RegistrationState> state_;

        friend class detail::PortDispatch;
    };

    struct Register
    {
        std::string package;
        References references;
    };

    struct Read
    {
        Registration& registration;
    };

    struct Emit
    {
        std::string package;
        Level level = Level::info;
        std::string type;
        References references;
        nlohmann::json data;
    };

    struct Close
    {
        Registration& registration;
    };

    namespace detail
    {
        class PortDispatch
        {
        private:
            static Registration apply(Register&& operation);
            static EventPtr apply(Read&& operation);
            static EventPtr apply(Emit&& operation);
            static void apply(Close&& operation);

            template <typename Operation>
            friend auto event_port::port(Operation&& operation);
        };
    }

    template <typename Operation>
    auto port(Operation&& operation)
    {
        static_assert(
            !std::is_lvalue_reference_v<Operation>,
            "event_port: operation must be passed as an rvalue");

        using Type = std::remove_cvref_t<Operation>;

        if constexpr (std::is_same_v<Type, Register>)
        {
            return detail::PortDispatch::apply(
                std::forward<Operation>(operation));
        }
        else if constexpr (std::is_same_v<Type, Read>)
        {
            return detail::PortDispatch::apply(
                std::forward<Operation>(operation));
        }
        else if constexpr (std::is_same_v<Type, Emit>)
        {
            return detail::PortDispatch::apply(
                std::forward<Operation>(operation));
        }
        else if constexpr (std::is_same_v<Type, Close>)
        {
            return detail::PortDispatch::apply(
                std::forward<Operation>(operation));
        }
        else
        {
            static_assert(
                detail::unsupported_operation<Type>,
                "event_port: unsupported operation");
        }
    }

    struct Event
    {
        std::uint64_t sequence = 0;
        std::chrono::system_clock::time_point timestamp;
        std::string package;
        Level level = Level::info;
        std::string type;
        References references;
        nlohmann::json data;

        ~Event() = default;

        Event(const Event&) = delete;
        Event& operator=(const Event&) = delete;
        Event(Event&&) = delete;
        Event& operator=(Event&&) = delete;

    private:
        Event(
            std::uint64_t sequence,
            std::chrono::system_clock::time_point timestamp,
            std::string&& package,
            Level level,
            std::string&& type,
            References&& references,
            nlohmann::json&& data);

        friend struct detail::EventAccess;
    };
}
