#include "request.h"
#include <secrets>

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
        secrets::SecureString api_key =
            secrets::resolve_secure_session(api_key_signature);

        return provider::compaction(
            selected_provider,
            endpoint,
            model_id,
            api_key.view(),
            compaction_prompt,
            session_current,
            tool_definitions,
            history,
            response_sink,
            compact,
            summary_sink,
            compaction_response);
    }
}
