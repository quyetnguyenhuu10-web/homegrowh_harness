#include "compaction.h"
#include "buld_transcript.h"
#include "request/catalog.h"

#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace provider
{
    namespace
    {
        struct CompactionModel
        {
            std::string endpoint;
            std::string api_key;
        };

        CompactionModel compaction_model(const std::string& model_id)
        {
            std::ifstream file(catalog_path());

            if (!file)
            {
                throw std::runtime_error("cannot open catalog.json");
            }

            nlohmann::json catalog;
            file >> catalog;

            const nlohmann::json* selected = nullptr;
            const nlohmann::json* selected_provider = nullptr;

            for (const auto& [provider_name, provider_entry] : catalog.items())
            {
                (void)provider_name;

                if (!provider_entry.is_object())
                {
                    throw std::runtime_error(
                        "catalog provider entry is not an object");
                }

                const nlohmann::json& models = provider_entry.at("models");

                if (!models.is_array())
                {
                    throw std::runtime_error(
                        "catalog provider models is not an array");
                }

                for (const nlohmann::json& model : models)
                {
                    if (model.at("id").get<std::string>() != model_id)
                    {
                        continue;
                    }

                    if (selected != nullptr)
                    {
                        throw std::runtime_error(
                            "duplicate model id in catalog: " + model_id);
                    }

                    selected = &model;
                    selected_provider = &provider_entry;
                }
            }

            if (selected == nullptr || selected_provider == nullptr)
            {
                throw std::runtime_error(
                    "model id not found in catalog: " + model_id);
            }

            return CompactionModel{
                selected_provider->at("endpoint").get<std::string>(),
                selected_provider->at("api_key").get<std::string>()};
        }

        std::string compaction_prompt()
        {
            std::ifstream file("src/compaction/COMPACTION.md");

            if (!file)
            {
                throw std::runtime_error(
                    "cannot open src/compaction/COMPACTION.md");
            }

            return std::string(
                std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
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

    }

    CompactionResult compaction(
        const std::string& model_id,
        const nlohmann::json& messages,
        RawResponse* raw_response,
        bool compact,
        const nlohmann::json& current_sessions,
        const nlohmann::json& tools)
    {
        const CompactionModel model = compaction_model(model_id);
        if (!compact)
        {
            nlohmann::json body = {
                {"model", model_id},
                {"messages", messages},
                {"stream", true}
            };

            if (!tools.empty())
            {
                body["tools"] = tools;
            }

            return CompactionResult{
                messages,
                request(
                    model.endpoint,
                    model.api_key,
                    body,
                    raw_response)};
        }

        nlohmann::json transcript = build_transcript(messages);
        transcript.at("content").get_ref<std::string&>() +=
            "\n\n" + compaction_prompt();

        nlohmann::json summary_body = {
            {"model", model_id},
            {"messages", nlohmann::json::array({transcript})},
            {"stream", true}
        };

        const auto summary_from_response = [](const RawResponse& response)
        {
            std::string summary;
            std::size_t offset = 0;

            for (const std::size_t bytes : response.event_sizes)
            {
                append_summary_text(
                    std::string_view(response.buffer).substr(offset, bytes),
                    &summary);
                offset += bytes;
            }

            return summary;
        };

        RequestUsage usage = UsageState::unavailable;
        std::string summary;
        const bool has_current_sessions = !current_sessions.empty();

        if (raw_response != nullptr && !has_current_sessions)
        {
            usage = request(
                model.endpoint,
                model.api_key,
                summary_body,
                raw_response);

            summary = summary_from_response(*raw_response);
        }
        else
        {
            RawResponse response;
            CompletionPort completion_port;
            std::optional<RequestUsage> internal_usage;
            std::exception_ptr request_error;

            completion_port.register_response(&response);

            std::thread request_thread([&]
            {
                try
                {
                    internal_usage = request(
                        model.endpoint,
                        model.api_key,
                        summary_body,
                        &response);
                }
                catch (...)
                {
                    request_error = std::current_exception();
                }
            });

            bool finished = false;

            while (!finished)
            {
                Completion completion;
                completion_port.wait(&completion);

                if (
                    completion.type == CompletionType::finished ||
                    completion.type == CompletionType::failed)
                {
                    finished = true;
                    continue;
                }

                read(
                    completion.response,
                    [](std::string_view)
                    {
                    });
            }

            request_thread.join();

            if (request_error != nullptr)
            {
                std::rethrow_exception(request_error);
            }

            if (!internal_usage.has_value())
            {
                throw std::runtime_error(
                    "compaction request completed without usage");
            }

            usage = std::move(*internal_usage);
            summary = summary_from_response(response);
        }

        if (summary.empty())
        {
            throw std::runtime_error("compaction produced an empty summary");
        }

        nlohmann::json compacted_messages = nlohmann::json::array();
        compacted_messages.push_back({
            {"role", "assistant"},
            {"content", std::move(summary)}
        });

        if (!current_sessions.empty() && current_sessions.is_array())
        {
            for (const nlohmann::json& session : current_sessions)
            {
                compacted_messages.push_back(session);
            }
        }
        else if (!current_sessions.empty())
        {
            compacted_messages.push_back(current_sessions);
        }

        if (has_current_sessions)
        {
            nlohmann::json request_body = {
                {"model", model_id},
                {"messages", compacted_messages},
                {"stream", true}
            };

            if (!tools.empty())
            {
                request_body["tools"] = tools;
            }

            usage = request(
                model.endpoint,
                model.api_key,
                request_body,
                raw_response);
        }

        return CompactionResult{
            std::move(compacted_messages),
            std::move(usage)};
    }
}
