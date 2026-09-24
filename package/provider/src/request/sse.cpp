#include "sse.h"

#include <cpr/cpr.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>

namespace provider
{
    namespace
    {
        struct EventDispatch
        {
            void* context;
            SSE::EventHandler on_event;
            std::exception_ptr error;
        };
    }

    void SSE::post(
        const std::string& url,
        const std::string& api_key,
        const nlohmann::json& body,
        void* context,
        EventHandler on_event,
        std::uint32_t* error)
    {
        if (error != nullptr)
        {
            *error = 0;
        }

        EventDispatch dispatch{context, on_event, nullptr};

        cpr::Session session;
        session.SetUrl(cpr::Url{url});
        session.SetHeader(cpr::Header{
            {"Accept", "text/event-stream"},
            {"Content-Type", "application/json"},
            {"Authorization", api_key}
        });
        session.SetBody(cpr::Body{body.dump()});
        session.SetServerSentEventCallback(
            cpr::ServerSentEventCallback{
                [](cpr::ServerSentEvent&& event, std::intptr_t userdata)
                {
                    auto* dispatch = reinterpret_cast<EventDispatch*>(userdata);

                    try
                    {
                        dispatch->on_event(
                            dispatch->context,
                            std::move(event.data));
                        return true;
                    }
                    catch (...)
                    {
                        dispatch->error = std::current_exception();
                        return false;
                    }
                },
                reinterpret_cast<std::intptr_t>(&dispatch)
            });

        cpr::Response response = session.Post();

        if (dispatch.error != nullptr)
        {
            std::rethrow_exception(dispatch.error);
        }

        if (response.error.code != cpr::ErrorCode::OK)
        {
            if (error != nullptr)
            {
                *error = static_cast<std::uint32_t>(response.error.code);
            }

            throw std::runtime_error(response.error.message);
        }

        /* HTTP failure is a request failure; this layer never hides it. */
        if (response.status_code < 200 || response.status_code >= 300)
        {
            if (error != nullptr)
            {
                *error = static_cast<std::uint32_t>(response.status_code);
            }

            throw std::runtime_error(
                "HTTP request failed with status " +
                std::to_string(response.status_code));
        }
    }
}
