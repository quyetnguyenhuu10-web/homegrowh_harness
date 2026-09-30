#include "compaction.h"
#include "buld_transcript.h"

#include "error/capture.h"
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

        Result<CurrentSession> current_session(const nlohmann::json& session_current)
        {
            if (session_current.empty())
            {
                return Result<CurrentSession>::success(CurrentSession{});
            }

            if (!session_current.is_object())
            {
                return Result<CurrentSession>::failure(error_detail::make_error(
                    "compaction", "invalid_argument",
                    "session_current must be an object",
                    {{{"field", "session_current"}, {"value", session_current}}}));
            }

            CurrentSession result;

            const auto messages = session_current.find("messages");
            if (messages != session_current.end())
            {
                if (!messages->is_array())
                {
                    return Result<CurrentSession>::failure(error_detail::make_error(
                        "compaction", "invalid_argument",
                        "session_current.messages must be an array",
                        {{{"field", "session_current.messages"}, {"value", *messages}}}));
                }
                result.messages = &*messages;
            }

            return Result<CurrentSession>::success(std::move(result));
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

        Result<void> receive_summary_event(void* context, std::string&& event)
        {
            auto* dispatch = static_cast<SummaryDispatch*>(context);
            try
            {
                append_summary_text(event, dispatch->summary);
                if (dispatch->downstream.on_event != nullptr)
                {
                    return dispatch->downstream.on_event(
                        dispatch->downstream.context, std::move(event));
                }
                return Result<void>::success();
            }
            catch (...)
            {
                return Result<void>::failure(error_detail::capture_exception(
                    std::current_exception(), "receive_summary",
                    {{{"event", event}}}));
            }
        }

        Result<void> finish_summary(void* context)
        {
            auto* dispatch = static_cast<SummaryDispatch*>(context);
            if (dispatch->downstream.on_finished != nullptr)
            {
                return dispatch->downstream.on_finished(dispatch->downstream.context);
            }
            return Result<void>::success();
        }

    }

    Result<CompactionResult> compaction(
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
        try
        {
            if (compaction_response != nullptr)
            {
                compaction_response->usage = UsageState::unavailable;
            }

            if (!history.empty() && !history.is_array())
            {
                return Result<CompactionResult>::failure(error_detail::make_error(
                    "compaction", "invalid_argument", "history must be a message array",
                    {{{"field", "history"}, {"value", history}}}));
            }
            if (!tool_definitions.empty() && !tool_definitions.is_array())
            {
                return Result<CompactionResult>::failure(error_detail::make_error(
                    "compaction", "invalid_argument", "tool_definitions must be an array",
                    {{{"field", "tool_definitions"}, {"value", tool_definitions}}}));
            }
            if (endpoint.empty())
            {
                return Result<CompactionResult>::failure(error_detail::make_error(
                    "compaction", "invalid_argument", "endpoint must not be empty",
                    {{{"field", "endpoint"}, {"value", endpoint}}}));
            }
            if (model_id.empty())
            {
                return Result<CompactionResult>::failure(error_detail::make_error(
                    "compaction", "invalid_argument", "model_id must not be empty",
                    {{{"field", "model_id"}, {"value", model_id}}}));
            }

            auto current_result = current_session(session_current);
            if (!current_result)
            {
                return Result<CompactionResult>::failure(std::move(*current_result.error));
            }
            const CurrentSession& current = *current_result.value;
            const bool has_tool_definitions = !tool_definitions.empty();
            const bool has_current_messages =
                current.messages != nullptr && !current.messages->empty();

            if (
                compact &&
                !history.empty() &&
                !has_current_messages &&
                !has_tool_definitions)
            {
                return Result<CompactionResult>::failure(error_detail::make_error(
                    "compaction", "invalid_argument",
                    "compact requires session_current messages or tool_definitions",
                    {{{"field", "session_current"}, {"value", session_current}}}));
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

                auto requested = request(
                    selected_provider,
                    endpoint,
                    api_key,
                    body,
                    response_sink);

                if (!requested)
                {
                    return Result<CompactionResult>::failure(error_detail::dependency_error(
                        "compaction", "Response request failed", std::move(*requested.error),
                        {{{"phase", "response_request"}, {"model", model_id}}}));
                }
                return Result<CompactionResult>::success(CompactionResult{
                    std::move(body.at("messages")), std::move(*requested.value)});
            }

            auto built = build_transcript(history);
            if (!built)
            {
                return Result<CompactionResult>::failure(error_detail::dependency_error(
                    "compaction", "Transcript construction failed", std::move(*built.error),
                    {{{"phase", "build_transcript"}}}));
            }
            nlohmann::json transcript = std::move(*built.value);
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

            auto summary_request = request(
                selected_provider,
                endpoint,
                api_key,
                summary_body,
                EventSink{
                    &summary_dispatch,
                    &receive_summary_event,
                    &finish_summary
                });

            if (!summary_request)
            {
                return Result<CompactionResult>::failure(error_detail::dependency_error(
                    "compaction", "Summary request failed", std::move(*summary_request.error),
                    {{{"phase", "summary_request"}, {"model", model_id}}}));
            }
            compacted.usage = std::move(*summary_request.value);

            if (std::holds_alternative<UsageState>(compacted.usage))
            {
                return Result<CompactionResult>::failure(error_detail::make_error(
                    "compaction", "protocol_error",
                    "compaction request completed without usage",
                    {{{"phase", "summary_request"}}}));
            }

            if (summary.empty())
            {
                return Result<CompactionResult>::failure(error_detail::make_error(
                    "compaction", "protocol_error", "compaction produced an empty summary",
                    {{{"phase", "summary_request"}}}));
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

            auto requested = request(
                selected_provider,
                endpoint,
                api_key,
                request_body,
                response_sink);

            if (!requested)
            {
                return Result<CompactionResult>::failure(error_detail::dependency_error(
                    "compaction", "Response request failed", std::move(*requested.error),
                    {{{"phase", "response_request"}, {"model", model_id}}}));
            }
            return Result<CompactionResult>::success(CompactionResult{
                std::move(compacted_messages), std::move(*requested.value)});
        }
        catch (...)
        {
            return Result<CompactionResult>::failure(error_detail::capture_exception(
                std::current_exception(), "compaction",
                {{{"endpoint", endpoint}, {"model", model_id}}}));
        }
    }
}
