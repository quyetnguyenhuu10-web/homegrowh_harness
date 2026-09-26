#include "requests.h"
#include "request_state.h"

#include <stdexcept>
#include <utility>

namespace provider
{
    HttpError::HttpError()
        : std::runtime_error("provider HTTP request failed")
    {
    }

    HttpError::HttpError(
        long status_code_value,
        std::string&& status_line_value,
        std::string&& reason_value,
        std::string&& body_value)
        : std::runtime_error(
              "HTTP request failed with status " +
              std::to_string(status_code_value)),
          status_code(status_code_value),
          status_line(std::move(status_line_value)),
          reason(std::move(reason_value)),
          body(std::move(body_value))
    {
    }

    RequestUsage request(
        Provider provider,
        const std::string& url,
        std::string_view api_key,
        const nlohmann::json& body,
        EventSink sink)
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
        RequestUsage usage = request_state.send(
            url,
            api_key,
            request_body,
            provider,
            sink);

        if (sink.on_finished != nullptr)
        {
            sink.on_finished(sink.context);
        }

        return usage;
    }
}
