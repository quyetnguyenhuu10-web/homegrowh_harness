#include "compaction.h"
#include "buld_transcript.h"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace provider
{
    namespace
    {
        void append_messages(
            nlohmann::json& destination,
            const nlohmann::json& source)
        {
            if (source.empty())
            {
                return;
            }

            if (source.is_array())
            {
                for (const nlohmann::json& message : source)
                {
                    destination.push_back(message);
                }
                return;
            }

            destination.push_back(source);
        }

        struct CurrentSession
        {
            const nlohmann::json* messages = nullptr;
        };

        CurrentSession current_session(const nlohmann::json& session_current)
        {
            if (session_current.empty())
            {
                return {};
            }

            if (!session_current.is_object())
            {
                throw std::invalid_argument(
                    "session_current must be an object");
            }

            CurrentSession result;

            const auto messages = session_current.find("messages");
            if (messages != session_current.end())
            {
                if (!messages->is_array())
                {
                    throw std::invalid_argument(
                        "session_current.messages must be an array");
                }
                result.messages = &*messages;
            }

            return result;
        }

        void append_summary_text(
            std::string_view event,
            std::string* summary)
        {
            const nlohmann::json payload = nlohmann::json::parse(
                event,
                nullptr,
                false);

            if (payload.is_discarded())
            {
                return;
            }

            const auto choices = payload.find("choices");

            if (choices == payload.end() || !choices->is_array())
            {
                return;
            }

            for (const nlohmann::json& choice : *choices)
            {
                const auto delta = choice.find("delta");

                if (delta == choice.end() || !delta->is_object())
                {
                    continue;
                }

                const auto content = delta->find("content");

                if (content != delta->end() && content->is_string())
                {
                    *summary += content->get_ref<const std::string&>();
                }
            }
        }

        struct SummaryDispatch
        {
            std::string* summary = nullptr;
            EventSink downstream;
        };

        void receive_summary_event(void* context, std::string&& event)
        {
            auto* dispatch = static_cast<SummaryDispatch*>(context);
            append_summary_text(event, dispatch->summary);

            if (dispatch->downstream.on_event != nullptr)
            {
                dispatch->downstream.on_event(
                    dispatch->downstream.context,
                    std::move(event));
            }
        }

        void finish_summary(void* context)
        {
            auto* dispatch = static_cast<SummaryDispatch*>(context);

            if (dispatch->downstream.on_finished != nullptr)
            {
                dispatch->downstream.on_finished(
                    dispatch->downstream.context);
            }
        }

    }

    CompactionResult compaction(
        Provider selected_provider,
        const std::string& endpoint,
        const std::string& model_id,
        std::string_view api_key,
        std::string_view compaction_prompt,
        const nlohmann::json& session_current,
        const nlohmann::json& tool_definitions,
        const nlohmann::json& history,
        EventSink response_sink,
        bool compact,
        EventSink summary_sink,
        CompactionResponse* compaction_response)
    {
        if (compaction_response != nullptr)
        {
            compaction_response->usage = UsageState::unavailable;
        }

        if (!history.empty() && !history.is_array())
        {
            throw std::invalid_argument("history must be a message array");
        }
        if (!tool_definitions.empty() && !tool_definitions.is_array())
        {
            throw std::invalid_argument("tool_definitions must be an array");
        }
        if (endpoint.empty())
        {
            throw std::invalid_argument("endpoint must not be empty");
        }
        if (model_id.empty())
        {
            throw std::invalid_argument("model_id must not be empty");
        }

        const CurrentSession current = current_session(session_current);
        const bool has_tool_definitions = !tool_definitions.empty();
        const bool has_current_messages =
            current.messages != nullptr && !current.messages->empty();

        if (
            compact &&
            !history.empty() &&
            !has_current_messages &&
            !has_tool_definitions)
        {
            throw std::invalid_argument(
                "compact requires session_current messages or tool_definitions");
        }

        if (!compact || history.empty())
        {
            nlohmann::json request_history = nlohmann::json::array();
            if (!history.empty())
            {
                append_messages(request_history, history);
            }
            if (current.messages != nullptr)
            {
                append_messages(request_history, *current.messages);
            }

            nlohmann::json body = {
                {"model", model_id},
                {"messages", std::move(request_history)},
                {"stream", true}
            };

            if (has_tool_definitions)
            {
                body["tools"] = tool_definitions;
            }

            RequestUsage usage = request(
                selected_provider,
                endpoint,
                api_key,
                body,
                response_sink);

            return CompactionResult{
                std::move(body.at("messages")),
                std::move(usage)};
        }

        nlohmann::json transcript = build_transcript(history);
        std::string& transcript_content =
            transcript.at("content").get_ref<std::string&>();
        transcript_content += "\n\n";
        transcript_content.append(
            compaction_prompt.data(),
            compaction_prompt.size());

        nlohmann::json summary_body = {
            {"model", model_id},
            {"messages", nlohmann::json::array({transcript})},
            {"stream", true}
        };

        std::string summary;
        SummaryDispatch summary_dispatch{
            &summary,
            summary_sink
        };

        CompactionResponse local_compaction_response;
        CompactionResponse& compacted = compaction_response == nullptr
            ? local_compaction_response
            : *compaction_response;

        compacted.usage = request(
            selected_provider,
            endpoint,
            api_key,
            summary_body,
            EventSink{
                &summary_dispatch,
                &receive_summary_event,
                &finish_summary
            });

        if (std::holds_alternative<UsageState>(compacted.usage))
        {
            throw std::runtime_error(
                "compaction request completed without usage");
        }

        if (summary.empty())
        {
            throw std::runtime_error("compaction produced an empty summary");
        }

        nlohmann::json compacted_messages = nlohmann::json::array();
        compacted_messages.push_back({
            {"role", "user"},
            {"content", std::move(summary)}
        });
        if (current.messages != nullptr)
        {
            append_messages(compacted_messages, *current.messages);
        }

        nlohmann::json request_body = {
            {"model", model_id},
            {"messages", compacted_messages},
            {"stream", true}
        };

        if (has_tool_definitions)
        {
            request_body["tools"] = tool_definitions;
        }

        RequestUsage usage = request(
            selected_provider,
            endpoint,
            api_key,
            request_body,
            response_sink);

        return CompactionResult{
            std::move(compacted_messages),
            std::move(usage)};
    }
}
