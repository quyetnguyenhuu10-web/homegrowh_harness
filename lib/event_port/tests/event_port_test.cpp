#include <event_port>

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace
{
    using namespace std::chrono_literals;

    void require(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }

    template <typename Operation>
    auto checked_port(Operation&& operation)
    {
        auto result = event_port::port(std::forward<Operation>(operation));
        if (!result)
        {
            std::cerr << nlohmann::json(*result.error).dump() << '\n';
            std::exit(EXIT_FAILURE);
        }
        if constexpr (!std::is_same_v<std::remove_cvref_t<Operation>, event_port::Close>)
            return std::move(*result.value);
    }

    event_port::References request_reference(const char* value)
    {
        event_port::References references;
        std::string type = "request_id";
        std::string id = value;
        references.emplace_back(std::move(type), std::move(id));
        return references;
    }

    event_port::Emit provider_event(
        const char* type_value,
        const char* request_id,
        int value)
    {
        return event_port::Emit{
            "provider",
            event_port::Level::info,
            type_value,
            request_reference(request_id),
            nlohmann::json{{"value", value}}
        };
    }
}

int main()
{
    event_port::Registration registration = checked_port(
        event_port::Register{
            "provider",
            request_reference("request-7")
        });

    const auto before = std::chrono::system_clock::now();
    std::future<event_port::EventPtr> reader = std::async(
        std::launch::async,
        [&]
        {
            return checked_port(event_port::Read{registration});
        });

    event_port::EventPtr emitted = checked_port(
        provider_event("data", "request-7", 1));
    event_port::EventPtr consumed = reader.get();
    const auto after = std::chrono::system_clock::now();

    require(emitted != nullptr, "event_port: emitted event is null");
    require(consumed.get() == emitted.get(), "event_port: event was copied");
    require(emitted->sequence == 0, "event_port: first sequence must be zero");
    require(
        emitted->timestamp >= before && emitted->timestamp <= after,
        "event_port: timestamp is outside emit interval");
    require(emitted->package == "provider", "event_port: package mismatch");
    require(emitted->type == "data", "event_port: type mismatch");
    require(emitted->references.size() == 1, "event_port: reference mismatch");
    require(emitted->data.at("value") == 1, "event_port: data mismatch");

    event_port::EventPtr first = checked_port(
        provider_event("data", "request-7", 2));

    std::future<event_port::EventPtr> blocked_writer = std::async(
        std::launch::async,
        []
        {
            return checked_port(
                provider_event("data", "request-7", 3));
        });

    require(
        blocked_writer.wait_for(50ms) == std::future_status::timeout,
        "event_port: producer must wait while subscriber slot is occupied");

    event_port::EventPtr first_read = checked_port(
        event_port::Read{registration});
    require(first_read.get() == first.get(), "event_port: wrong pending event");

    require(
        blocked_writer.wait_for(1s) == std::future_status::ready,
        "event_port: producer was not released after read");

    event_port::EventPtr second = blocked_writer.get();
    event_port::EventPtr second_read = checked_port(
        event_port::Read{registration});
    require(second_read.get() == second.get(), "event_port: second event mismatch");
    require(
        second->sequence == first->sequence + 1,
        "event_port: sequence must remain monotonic");

    event_port::EventPtr unmatched = checked_port(
        provider_event("data", "other-request", 4));
    require(unmatched != nullptr, "event_port: unmatched emit failed");

    event_port::EventPtr pending_before_move = checked_port(
        provider_event("data", "request-7", 5));

    std::future<event_port::EventPtr> blocked_before_move = std::async(
        std::launch::async,
        []
        {
            return checked_port(
                provider_event("data", "request-7", 6));
        });

    require(
        blocked_before_move.wait_for(50ms) == std::future_status::timeout,
        "event_port: producer should be blocked before registration move");

    event_port::Registration replacement = checked_port(
        event_port::Register{
            "provider",
            request_reference("replacement-request")
        });

    registration = std::move(replacement);

    require(
        blocked_before_move.wait_for(1s) == std::future_status::ready,
        "event_port: closing old registration did not release producer");
    require(
        blocked_before_move.get() != nullptr,
        "event_port: released producer returned null");
    require(
        pending_before_move != nullptr,
        "event_port: pending event was unexpectedly invalidated");

    event_port::Registration close_registration = checked_port(
        event_port::Register{
            "provider",
            request_reference("close-request")
        });

    std::future<bool> blocked_reader = std::async(
        std::launch::async,
        [&]
        {
            const auto read = event_port::port(event_port::Read{close_registration});
            return !read.value && read.error && read.error->type == "registration_closed";
        });

    require(
        blocked_reader.wait_for(50ms) == std::future_status::timeout,
        "event_port: reader should block before close");

    checked_port(event_port::Close{close_registration});

    require(
        blocked_reader.wait_for(1s) == std::future_status::ready,
        "event_port: close did not release blocked reader");
    require(
        blocked_reader.get(),
        "event_port: closed reader did not report closure");

    event_port::Registration drain_registration = checked_port(
        event_port::Register{
            "provider",
            request_reference("drain-request")
        });

    event_port::EventPtr pending_before_close = checked_port(
        provider_event("data", "drain-request", 7));

    checked_port(event_port::Close{drain_registration});

    event_port::EventPtr drained = checked_port(
        event_port::Read{drain_registration});
    require(
        drained.get() == pending_before_close.get(),
        "event_port: close discarded pending event");

    const auto closed_after_drain = event_port::port(event_port::Read{drain_registration});
    require(
        !closed_after_drain.value && closed_after_drain.error &&
        closed_after_drain.error->type == "registration_closed",
        "event_port: read after draining closed registration must fail");

    event_port::Registration writer_registration = checked_port(
        event_port::Register{
            "provider",
            request_reference("close-writer")
        });

    event_port::EventPtr occupied = checked_port(
        provider_event("data", "close-writer", 8));

    std::future<event_port::EventPtr> blocked_writer_on_close = std::async(
        std::launch::async,
        []
        {
            return checked_port(
                provider_event("data", "close-writer", 9));
        });

    require(
        blocked_writer_on_close.wait_for(50ms) == std::future_status::timeout,
        "event_port: writer should block before close");

    checked_port(event_port::Close{writer_registration});

    require(
        blocked_writer_on_close.wait_for(1s) == std::future_status::ready,
        "event_port: close did not release blocked writer");
    require(
        blocked_writer_on_close.get() != nullptr,
        "event_port: released writer returned null event");

    event_port::EventPtr occupied_after_close = checked_port(
        event_port::Read{writer_registration});
    require(
        occupied_after_close.get() == occupied.get(),
        "event_port: close must preserve the occupied pending event");

    return EXIT_SUCCESS;
}
