#include "sse.h"
#include "requests.h"

#include <cpr/cpr.h>

#include <charconv>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
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
            bool http_failure = false;
            long status_code = 0;
            std::string status_line;
            std::string reason;
            std::string http_error_body;
        };

        void receive_header(EventDispatch& dispatch, std::string_view header)
        {
            if (header.size() < 5 || header.substr(0, 5) != "HTTP/")
                return;

            while (!header.empty()
                && (header.back() == '\r' || header.back() == '\n'))
            {
                header.remove_suffix(1);
            }

            const std::size_t first_space = header.find(' ');
            if (first_space == std::string_view::npos)
                return;

            const std::size_t code_begin = first_space + 1;
            const std::size_t code_end = header.find(' ', code_begin);
            const std::string_view code_text = header.substr(
                code_begin,
                code_end == std::string_view::npos
                    ? std::string_view::npos
                    : code_end - code_begin);

            long status_code = 0;
            const char* begin = code_text.data();
            const char* end = begin + code_text.size();
            const auto parsed = std::from_chars(begin, end, status_code);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
                return;

            dispatch.http_failure = status_code < 200 || status_code >= 300;
            dispatch.status_code = status_code;
            dispatch.status_line.assign(header);
            dispatch.reason =
                code_end == std::string_view::npos
                    ? std::string{}
                    : std::string(header.substr(code_end + 1));
            dispatch.http_error_body.clear();
        }
    }

    void SSE::post(
        const std::string& url,
        std::string_view api_key,
        const nlohmann::json& body,
        void* context,
        EventHandler on_event)
    {
        EventDispatch dispatch{
            context,
            on_event,
            nullptr,
            false,
            0,
            {},
            {},
            {}};

        cpr::Session session;
        session.SetUrl(cpr::Url{url});
        session.SetHeader(cpr::Header{
            {"Accept", "text/event-stream"},
            {"Content-Type", "application/json"},
            {"Authorization", std::string(api_key)}
        });
        session.SetBody(cpr::Body{body.dump()});
        session.SetHeaderCallback(
            cpr::HeaderCallback{
                [](std::string_view header, std::intptr_t userdata)
                {
                    auto* dispatch = reinterpret_cast<EventDispatch*>(userdata);
                    receive_header(*dispatch, header);
                    return true;
                },
                reinterpret_cast<std::intptr_t>(&dispatch)
            });

        cpr::ServerSentEventCallback sse{
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
        };

        session.SetWriteCallback(
            cpr::WriteCallback{
                [&](std::string_view data, std::intptr_t)
                {
                    if (dispatch.http_failure)
                    {
                        try
                        {
                            dispatch.http_error_body.append(data);
                            return true;
                        }
                        catch (...)
                        {
                            dispatch.error = std::current_exception();
                            return false;
                        }
                    }

                    return sse.handleData(data);
                }
            });

        cpr::Response response = session.Post();

        if (dispatch.error != nullptr)
        {
            std::rethrow_exception(dispatch.error);
        }

        if (response.error.code != cpr::ErrorCode::OK)
        {
            throw std::runtime_error(response.error.message);
        }

        /* HTTP failure is a request failure; this layer never hides it. */
        if (response.status_code < 200 || response.status_code >= 300)
        {
            std::string status_line = dispatch.status_line.empty()
                ? std::move(response.status_line)
                : std::move(dispatch.status_line);
            std::string reason = dispatch.reason.empty()
                ? std::move(response.reason)
                : std::move(dispatch.reason);

            throw HttpError(
                response.status_code,
                std::move(status_line),
                std::move(reason),
                std::move(dispatch.http_error_body));
        }
    }
}
