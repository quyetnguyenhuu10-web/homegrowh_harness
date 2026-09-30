#include "sse.h"
#include "error/capture.h"

#include <cpr/cpr.h>

#include <charconv>
#include <system_error>

namespace provider
{
    namespace
    {
        struct EventDispatch
        {
            void* context;
            SSE::EventHandler on_event;
            std::optional<Error> error;
            bool http_failure = false;
            std::string status_line;
            std::string reason;
            std::string http_error_body;
        };

        void receive_header(EventDispatch& dispatch, std::string_view header)
        {
            if (!header.starts_with("HTTP/"))
            {
                return;
            }
            while (!header.empty()
                && (header.back() == '\r' || header.back() == '\n'))
            {
                header.remove_suffix(1);
            }
            const auto first_space = header.find(' ');
            if (first_space == std::string_view::npos)
            {
                return;
            }
            const auto code_end = header.find(' ', first_space + 1);
            const auto code_text = header.substr(
                first_space + 1, code_end == std::string_view::npos
                    ? std::string_view::npos : code_end - first_space - 1);
            long status_code = 0;
            const auto parsed = std::from_chars(
                code_text.data(), code_text.data() + code_text.size(), status_code);
            if (parsed.ec != std::errc{}
                || parsed.ptr != code_text.data() + code_text.size())
            {
                return;
            }
            dispatch.http_failure = status_code < 200 || status_code >= 300;
            dispatch.status_line.assign(header);
            dispatch.reason = code_end == std::string_view::npos
                ? std::string{} : std::string(header.substr(code_end + 1));
            dispatch.http_error_body.clear();
        }
    }

    Result<void> SSE::post(
        const std::string& url,
        std::string_view api_key,
        const nlohmann::json& body,
        void* context,
        EventHandler on_event)
    {
        try
        {
            EventDispatch dispatch{context, on_event, std::nullopt};
            cpr::Session session;
            session.SetUrl(cpr::Url{url});
            session.SetHeader(cpr::Header{
                {"Accept", "text/event-stream"},
                {"Content-Type", "application/json"},
                {"Authorization", std::string(api_key)}});
            session.SetBody(cpr::Body{body.dump()});
            session.SetHeaderCallback(cpr::HeaderCallback{
                [&](std::string_view header, std::intptr_t)
                {
                    try
                    {
                        receive_header(dispatch, header);
                        return true;
                    }
                    catch (...)
                    {
                        dispatch.error = error_detail::capture_exception(
                            std::current_exception(), "receive_header",
                            {{{"header", header}, {"url", url}}});
                        return false;
                    }
                }});

            cpr::ServerSentEventCallback sse{
                [&](cpr::ServerSentEvent&& event, std::intptr_t)
                {
                    if (dispatch.error.has_value())
                    {
                        return false;
                    }
                    auto result = dispatch.on_event(
                        dispatch.context, std::move(event.data));
                    if (!result)
                    {
                        dispatch.error = std::move(*result.error);
                        return false;
                    }
                    return true;
                }};

            session.SetWriteCallback(cpr::WriteCallback{
                [&](std::string_view data, std::intptr_t)
                {
                    if (dispatch.error.has_value())
                    {
                        return false;
                    }
                    try
                    {
                        if (dispatch.http_failure)
                        {
                            dispatch.http_error_body.append(data);
                            return true;
                        }
                        return sse.handleData(data);
                    }
                    catch (...)
                    {
                        dispatch.error = error_detail::capture_exception(
                            std::current_exception(), "receive_event",
                            {{{"chunk", data}, {"url", url}}});
                        return false;
                    }
                }});

            // CPR owns the handle. Retain CURLcode before CPR maps its enum.
            session.PreparePost();
            const auto curl = session.GetCurlHolder();
            const CURLcode curl_code = curl_easy_perform(curl->handle);
            if (dispatch.error.has_value())
            {
                return Result<void>::failure(std::move(*dispatch.error));
            }

            std::optional<Error> transport_error;
            if (curl_code != CURLE_OK)
            {
                transport_error = error_detail::make_error(
                    "request", "system_error",
                    curl->error.front() != '\0' ? curl->error.data()
                                              : curl_easy_strerror(curl_code),
                    {{{"code", static_cast<int>(curl_code)},
                      {"category", "libcurl"}, {"api", "curl_easy_perform"},
                      {"url", url}}});
                long os_error = 0;
                const CURLcode info_code = curl_easy_getinfo(
                    curl->handle, CURLINFO_OS_ERRNO, &os_error);
                if (info_code != CURLE_OK)
                {
                    transport_error->causes.push_back(error_detail::make_error(
                        "read_transport_error", "system_error",
                        curl_easy_strerror(info_code),
                        {{{"code", static_cast<int>(info_code)},
                          {"category", "libcurl"}, {"api", "curl_easy_getinfo"}}}));
                }
                else if (os_error != 0)
                {
                    transport_error->data.push_back({
                        {"code", os_error}, {"category", "system"},
                        {"api", "CURLINFO_OS_ERRNO"}});
                }
            }

            cpr::Response response = session.Complete(curl_code);
            if (transport_error.has_value())
            {
                transport_error->data.push_back({
                    {"code", static_cast<int>(response.error.code)},
                    {"category", "cpr"}, {"message", response.error.message}});
            }
            if (response.status_code != 0
                && (response.status_code < 200 || response.status_code >= 300))
            {
                Error error = error_detail::make_error(
                    "request", "http_error",
                    "HTTP request failed with status "
                        + std::to_string(response.status_code),
                    {{{"status_code", response.status_code},
                      {"status_line", dispatch.status_line.empty()
                          ? response.status_line : dispatch.status_line},
                      {"reason", dispatch.reason.empty()
                          ? response.reason : dispatch.reason},
                      {"body", std::move(dispatch.http_error_body)}, {"url", url}}});
                if (transport_error.has_value())
                {
                    error.causes.push_back(std::move(*transport_error));
                }
                return Result<void>::failure(std::move(error));
            }
            if (transport_error.has_value())
            {
                return Result<void>::failure(std::move(*transport_error));
            }
            return Result<void>::success();
        }
        catch (...)
        {
            return Result<void>::failure(error_detail::capture_exception(
                std::current_exception(), "request", {{{"url", url}}}));
        }
    }
}
