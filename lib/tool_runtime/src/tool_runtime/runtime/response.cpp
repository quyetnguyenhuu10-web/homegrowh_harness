#include "response.h"

#include "../error/error.h"

namespace tool_runtime::detail
{
    namespace
    {
        std::optional<Error> payload_error(
            const nlohmann::json& payload,
            std::string_view source,
            std::string_view operation)
        {
            if (!payload.is_object())
                return std::nullopt;
            std::vector<Error> causes;
            nlohmann::json context = nlohmann::json::object();
            for (const auto& [key, value] : payload.items())
            {
                if (key != "error" && key != "ok" && key != "results" && (key != "value" || !value.is_null()))
                    context[key] = value;
            }
            const auto reported = payload.find("error");
            if (reported != payload.end() && !reported->is_null())
            {
                Error error = adapt_error(*reported, source, operation);
                causes.push_back(std::move(error));
            }
            const auto results = payload.find("results");
            nlohmann::json successful = nlohmann::json::array();
            nlohmann::json failed_indexes = nlohmann::json::array();
            if (results != payload.end() && results->is_array())
            {
                for (std::size_t index = 0; index < results->size(); ++index)
                {
                    const auto& result = results->at(index);
                    if (auto error = payload_error(result, source, operation))
                    {
                        failed_indexes.push_back(index);
                        causes.push_back(std::move(*error));
                    }
                    else
                        successful.push_back({{"index", index}, {"result", result}});
                }
            }
            if (!causes.empty() && !successful.empty())
            {
                context["successful_results"] = std::move(successful);
                context["failed_result_indexes"] = std::move(failed_indexes);
            }
            if (causes.size() == 1)
            {
                if (!context.empty())
                {
                    // Context on the reported error itself does not introduce another cause.
                    if (reported != payload.end() && !reported->is_null() && results == payload.end())
                    {
                        causes.front().data.push_back(std::move(context));
                        return std::move(causes.front());
                    }
                    return dependency_error(operation, "Tool result entry failed", std::move(causes), std::move(context));
                }
                return std::move(causes.front());
            }
            if (!causes.empty())
                return dependency_error(operation, "Multiple tool operations failed", std::move(causes), std::move(context));

            const auto ok = payload.find("ok");
            if (ok != payload.end() && ok->is_boolean() && !ok->get<bool>())
                return make_error(operation, "protocol_error", "Tool reported failure without an error", {{"response", payload}});
            return std::nullopt;
        }
    }

    Result<nlohmann::json> normalize_result_payload(
        nlohmann::json&& payload,
        std::string_view source,
        std::string_view operation)
    {
        try
        {
            if (auto error = payload_error(payload, source, operation))
                return Result<nlohmann::json>::failure(std::move(*error));
            return Result<nlohmann::json>::success(std::move(payload));
        }
        catch (...)
        {
            Error error = current_exception_error(operation);
            error.data.push_back({{"response", payload}});
            return Result<nlohmann::json>::failure(std::move(error));
        }
    }

    Result<nlohmann::json> normalize_result_message(
        const nlohmann::json& message,
        std::string_view source,
        std::string_view operation)
    {
        if (!message.is_object())
            return Result<nlohmann::json>::failure(make_error(operation, "protocol_error",
                "Tool result message must be an object", {{"response", message}}));
        const auto content = message.find("content");
        if (content == message.end() || !content->is_string())
            return Result<nlohmann::json>::failure(make_error(operation, "protocol_error",
                "Tool result message must contain string content", {{"response", message}}));
        auto parsed = parse_json(content->get_ref<const std::string&>(), "parse_result_content");
        if (parsed.error)
            return Result<nlohmann::json>::success(text_payload(content->get_ref<const std::string&>()));
        return normalize_result_payload(std::move(*parsed.value), source, operation);
    }
}
