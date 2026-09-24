#include "requests.h"

#if defined(_WIN32)

#include "platform/windows/request/completion.h"

namespace provider
{
    namespace platform_router = windows;
}

#elif defined(__linux__)

#include "platform/linux/request/completion.h"

namespace provider
{
    namespace platform_router = linux;
}

#else

#error "Unsupported operating system"

#endif

#include "request_state.h"

#include <exception>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace provider
{
    RequestUsage request(
        Provider provider,
        const std::string& url,
        const std::string& api_key,
        const nlohmann::json& body,
        RawResponse* raw_response)
    {
        const std::string model_id = body.at("model").get<std::string>();
        if (model_id.empty())
        {
            throw std::invalid_argument("provider: model id must not be empty");
        }

        nlohmann::json request_body = body;

        if (
            provider != Provider::bonsai &&
            request_body.value("stream", false))
        {
            request_body["stream_options"]["include_usage"] = true;
        }

        RequestState request_state;
        std::optional<RequestUsage> usage;

        const std::uintptr_t completion_port =
            raw_response == nullptr
                ? 0
                : raw_response->completion_port;

        bool flight_started = false;

        if (raw_response != nullptr)
        {
            raw_response->event_sizes.clear();

            if (completion_port == 0)
            {
                throw std::logic_error(
                    "RawResponse must be registered with CompletionPort");
            }

            bool expected = false;

            if (!raw_response->in_flight.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire))
            {
                throw std::logic_error(
                    "RawResponse already has an in-flight request");
            }

            flight_started = true;
        }

        try
        {
            usage = request_state.send(
                url,
                api_key,
                request_body,
                raw_response,
                completion_port,
                provider);

            if (raw_response != nullptr)
            {
                raw_response->event_sizes = request_state.take_event_sizes();
            }

            /* Only a completely successful pipeline is allowed to finish. */
            platform_router::completion_post_finished(
                completion_port,
                raw_response);
        }
        catch (...)
        {
            const std::exception_ptr request_error =
                std::current_exception();

            std::uint32_t error = request_state.error();

            if (error == 0)
            {
                try
                {
                    std::rethrow_exception(request_error);
                }
                catch (const std::system_error& system_error)
                {
                    error = static_cast<std::uint32_t>(
                        system_error.code().value());
                }
                catch (...)
                {
                }
            }

            /*
             * Fail fast: do not retry, drain, or translate failure into
             * finished. The throwing request keeps the original error and
             * the completion consumer gets an explicit failed state.
             */
            try
            {
                platform_router::completion_post_failed(
                    completion_port,
                    raw_response,
                    error);
            }
            catch (...)
            {
                if (flight_started)
                {
                    raw_response->in_flight.store(
                        false,
                        std::memory_order_release);
                }

                /* Notification failure is itself a hard failure. */
                throw;
            }

            if (flight_started)
            {
                raw_response->in_flight.store(
                    false,
                    std::memory_order_release);
            }

            std::rethrow_exception(request_error);
        }

        if (flight_started)
        {
            raw_response->in_flight.store(
                false,
                std::memory_order_release);
        }

        return std::move(*usage);
    }
}
