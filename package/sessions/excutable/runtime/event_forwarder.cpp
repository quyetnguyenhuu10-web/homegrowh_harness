#include "event_forwarder.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace sessions_runtime
{
    namespace
    {
        std::vector<std::uint8_t> encode_event(
            const event_port::Event& event)
        {
            nlohmann::json references = nlohmann::json::array();
            for (const event_port::Reference& reference : event.references)
            {
                references.push_back(nlohmann::json::array({
                    reference.type,
                    reference.value
                }));
            }

            const auto timestamp_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    event.timestamp.time_since_epoch()).count();

            nlohmann::json wire = nlohmann::json::array({
                event.sequence,
                timestamp_ms,
                event.package,
                static_cast<std::uint8_t>(event.level),
                event.type,
                std::move(references),
                event.data
            });

            return nlohmann::json::to_cbor(wire);
        }
    }

    EventForwarder::EventForwarder(ipc::connection& connection)
        : connection_(connection)
    {
    }

    EventForwarder::~EventForwarder()
    {
        stop();
    }

    void EventForwarder::start()
    {
        if (started_)
            throw std::logic_error("event forwarder is already started");

        registration_.emplace(event_port::port(event_port::Register{
            "",
            {}
        }));
        started_ = true;
        thread_ = std::thread([this]
        {
            run();
        });
    }

    void EventForwarder::stop() noexcept
    {
        if (!started_)
            return;

        if (registration_.has_value())
        {
            try
            {
                event_port::port(event_port::Close{*registration_});
            }
            catch (...)
            {
            }
        }

        if (thread_.joinable())
            thread_.join();

        registration_.reset();
        started_ = false;
    }

    void EventForwarder::rethrow_if_failed() const
    {
        std::exception_ptr error;
        {
            std::lock_guard lock(error_mutex_);
            error = error_;
        }

        if (error != nullptr)
            std::rethrow_exception(error);
    }

    void EventForwarder::run() noexcept
    {
        try
        {
            for (;;)
            {
                const event_port::EventPtr event = event_port::port(
                    event_port::Read{*registration_});
                if (event == nullptr)
                    throw std::logic_error("event_port returned a null event");

                const std::vector<std::uint8_t> bytes =
                    encode_event(*event);
                const ipc::write_result written = ipc::write(
                    connection_,
                    std::span<const std::uint8_t>(bytes));
                if (written.error)
                {
                    throw std::system_error(
                        written.error,
                        "write EventPort event to IPC");
                }
            }
        }
        catch (const std::logic_error&)
        {
            // Closing the registration is the normal shutdown path.
        }
        catch (...)
        {
            set_error(std::current_exception());
            try
            {
                if (registration_.has_value())
                    event_port::port(event_port::Close{*registration_});
            }
            catch (...)
            {
            }
        }
    }

    void EventForwarder::set_error(std::exception_ptr error) noexcept
    {
        std::lock_guard lock(error_mutex_);
        if (error_ == nullptr)
            error_ = std::move(error);
    }
}
