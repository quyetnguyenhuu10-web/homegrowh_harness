#include "event_forwarder.h"
#include "ipc_failure.h"
#include <error/event_port.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace sessions_runtime
{
    namespace
    {
        sessions::Error forwarding_error(std::exception_ptr error)
        {
            try
            {
                std::rethrow_exception(error);
            }
            catch (const IpcFailure& failure)
            {
                return sessions::detail::convert_error<sessions::Error>(ipc::Error(failure.error()));
            }
            catch (const std::exception& exception)
            {
                return sessions::detail::exception_error("forward_events", exception);
            }
            catch (...)
            {
                return {"session_runtime", "forward_events", "unknown_exception", "", {}, {}};
            }
        }

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

        registration_.emplace(sessions::detail::checked_port(event_port::Register{
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
                sessions::detail::checked_port(event_port::Close{*registration_});
            }
            catch (...)
            {
                set_error(std::current_exception());
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
                auto read = event_port::port(
                    event_port::Read{*registration_});
                if (read.error)
                {
                    if (read.error->type == "registration_closed")
                        return;
                    throw sessions::ErrorException(sessions::detail::convert_error<sessions::Error>(
                        std::move(*read.error)));
                }
                const event_port::EventPtr event = std::move(*read.value);

                const std::vector<std::uint8_t> bytes =
                    encode_event(*event);
                ipc::write_result written = ipc::write(
                    connection_,
                    std::span<const std::uint8_t>(bytes));
                if (written.error)
                {
                    throw IpcFailure(std::move(*written.error));
                }
            }
        }
        catch (...)
        {
            set_error(std::current_exception());
            try
            {
                if (registration_.has_value())
                    sessions::detail::checked_port(event_port::Close{*registration_});
            }
            catch (...)
            {
                set_error(std::current_exception());
            }
        }
    }

    void EventForwarder::set_error(std::exception_ptr error) noexcept
    {
        std::lock_guard lock(error_mutex_);
        if (error_ == nullptr)
            error_ = std::move(error);
        else if (error != nullptr)
        {
            std::vector<sessions::Error> causes;
            causes.emplace_back(forwarding_error(error_));
            causes.emplace_back(forwarding_error(std::move(error)));
            error_ = std::make_exception_ptr(sessions::ErrorException(sessions::Error{
                "session_runtime", "forward_events", "dependency_error",
                "Event forwarding encountered multiple failures", {}, std::move(causes)}));
        }
    }
}
