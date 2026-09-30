#include "request.h"
#include <secrets>
#include <session>
#include <error/error.h>

#include <nlohmann/json.hpp>

namespace sessions
{
    provider::CompactionResult request(
        const std::string& api_key_signature,
        provider::Provider selected_provider,
        const std::string& endpoint,
        const std::string& model_id,
        std::string_view compaction_prompt,
        const nlohmann::json& session_current,
        const nlohmann::json& tool_definitions,
        bool compact,
        const nlohmann::json& history,
        provider::EventSink response_sink,
        provider::EventSink summary_sink,
        provider::CompactionResponse* compaction_response)
    {
        secrets::SecureSecretResult api_key =
            secrets::resolve_secure_session(api_key_signature);
        if (api_key.error)
        {
            throw ErrorException(detail::dependency_error(
                "request", "Session credential could not be resolved",
                std::move(*api_key.error)));
        }

        auto response = provider::compaction(
            selected_provider,
            endpoint,
            model_id,
            api_key.value->view(),
            compaction_prompt,
            session_current,
            tool_definitions,
            history,
            response_sink,
            compact,
            summary_sink,
            compaction_response);
        if (response.error)
        {
            throw ErrorException(detail::dependency_error(
                "request", "Provider request could not be completed",
                detail::convert_error<Error>(std::move(*response.error))));
        }
        return std::move(*response.value);
    }
}
