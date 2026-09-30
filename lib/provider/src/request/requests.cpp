#include "requests.h"
#include "request_state.h"
#include "error/capture.h"

namespace provider
{
    Result<RequestUsage> request(
        Provider selected_provider,
        const std::string& url,
        std::string_view api_key,
        const nlohmann::json& body,
        EventSink sink)
    {
        try
        {
            const std::string& model_id =
                body.at("model").get_ref<const std::string&>();
            if (model_id.empty() || url.empty())
            {
                return Result<RequestUsage>::failure(error_detail::make_error(
                    "request", "invalid_argument",
                    model_id.empty() ? "Model id must not be empty"
                                     : "URL must not be empty",
                    {{{"field", model_id.empty() ? "model" : "url"},
                      {"url", url}, {"body", body}}}));
            }

            auto prepared = prepare_request_body(selected_provider, body);
            if (!prepared)
            {
                return Result<RequestUsage>::failure(std::move(*prepared.error));
            }
            RequestState state(selected_provider, sink);
            auto result = state.send(url, api_key, *prepared.value);
            if (!result)
            {
                return result;
            }
            if (sink.on_finished != nullptr)
            {
                try
                {
                    auto finished = sink.on_finished(sink.context);
                    if (!finished)
                    {
                        return Result<RequestUsage>::failure(
                            std::move(*finished.error));
                    }
                }
                catch (...)
                {
                    return Result<RequestUsage>::failure(
                        error_detail::capture_exception(
                            std::current_exception(), "finish_request",
                            {{{"api", "EventSink::on_finished"}, {"url", url}}}));
                }
            }
            return result;
        }
        catch (...)
        {
            return Result<RequestUsage>::failure(error_detail::capture_exception(
                std::current_exception(), "request", {{{"url", url}, {"body", body}}}));
        }
    }
}
