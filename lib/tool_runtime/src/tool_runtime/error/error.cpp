#include "error.h"

#include <filesystem>
#include <typeinfo>

namespace tool_runtime
{
    void to_json(nlohmann::json& json, const Error& error)
    {
        json = {
            {"source", error.source},
            {"operation", error.operation},
            {"type", error.type},
            {"message", error.message},
            {"data", error.data},
            {"causes", error.causes}
        };
    }

    namespace
    {
        Result<Error> decode_error(
            const nlohmann::json& json,
            const std::string& path)
        {
            const auto invalid = [&json, &path](std::string_view field)
            {
                return Result<Error>::failure(detail::make_error(
                    "deserialize_error", "protocol_error",
                    "Error does not match the six-field schema",
                    {{"path", path}, {"field", field}, {"value", json}}));
            };
            if (!json.is_object() || json.size() != 6)
                return invalid("envelope");
            for (const char* field : {"source", "operation", "type", "message"})
            {
                const auto value = json.find(field);
                if (value == json.end() || !value->is_string())
                    return invalid(field);
            }
            for (const char* field : {"data", "causes"})
            {
                const auto value = json.find(field);
                if (value == json.end() || !value->is_array())
                    return invalid(field);
            }

            Error error{
                json.at("source").get<std::string>(),
                json.at("operation").get<std::string>(),
                json.at("type").get<std::string>(),
                json.at("message").get<std::string>(),
                json.at("data").get<nlohmann::json::array_t>(),
                {}};
            for (std::size_t index = 0; index < json.at("causes").size(); ++index)
            {
                auto cause = decode_error(
                    json.at("causes").at(index),
                    path + ".causes[" + std::to_string(index) + "]");
                if (cause.error)
                    return Result<Error>::failure(std::move(*cause.error));
                error.causes.push_back(std::move(*cause.value));
            }
            return Result<Error>::success(std::move(error));
        }
    }

    Result<Error> deserialize_error(const nlohmann::json& json)
    {
        try
        {
            return decode_error(json, "error");
        }
        catch (...)
        {
            return Result<Error>::failure(
                detail::current_exception_error("deserialize_error"));
        }
    }
}

namespace tool_runtime::detail
{
    nlohmann::json text_payload(std::string_view text)
    {
        nlohmann::json value = std::string(text);
        try
        {
            // JSON strings must be UTF-8. Preserve invalid output as bytes, without replacement.
            static_cast<void>(value.dump());
            return value;
        }
        catch (const nlohmann::json::type_error&)
        {
            nlohmann::json bytes = nlohmann::json::array();
            for (unsigned char byte : text)
                bytes.push_back(byte);
            return {{"encoding", "bytes"}, {"bytes", std::move(bytes)}};
        }
    }

    Error make_error(
        std::string_view operation,
        std::string_view type,
        std::string_view message,
        nlohmann::json&& details)
    {
        const auto encoded_message = text_payload(message);
        Error error{
            "tool_runtime", std::string(operation), std::string(type),
            encoded_message.is_string() ? std::string(message) : std::string("Error message contains non-UTF-8 bytes"), {}, {}};
        if (!details.is_object() || !details.empty())
            error.data.push_back(std::move(details));
        if (!encoded_message.is_string())
            error.data.push_back({{"message", encoded_message}});
        return error;
    }

    Error make_system_error(
        std::string_view operation,
        std::string_view api,
        const std::error_code& code,
        nlohmann::json&& details)
    {
        details["code"] = code.value();
        details["category"] = code.category().name();
        details["api"] = api;
        return make_error(operation, "system_error", code.message(), std::move(details));
    }

    Error exception_error(
        std::string_view operation,
        const std::exception& exception,
        nlohmann::json&& details)
    {
        details["exception_type"] = typeid(exception).name();
        if (const auto* system = dynamic_cast<const std::system_error*>(&exception))
        {
            details["code"] = system->code().value();
            details["category"] = system->code().category().name();
            if (const auto* filesystem =
                    dynamic_cast<const std::filesystem::filesystem_error*>(&exception))
            {
                const auto first = filesystem->path1().u8string();
                const auto second = filesystem->path2().u8string();
                details["path"] = std::string(reinterpret_cast<const char*>(first.data()), first.size());
                details["path2"] = std::string(reinterpret_cast<const char*>(second.data()), second.size());
            }
            return make_error(operation, "system_error", exception.what(), std::move(details));
        }
        if (const auto* json = dynamic_cast<const nlohmann::json::exception*>(&exception))
        {
            details["id"] = json->id;
            if (const auto* parse = dynamic_cast<const nlohmann::json::parse_error*>(json))
                details["byte"] = parse->byte;
            return make_error(operation, "protocol_error", exception.what(), std::move(details));
        }
        return make_error(operation, "exception_error", exception.what(), std::move(details));
    }

    Error current_exception_error(std::string_view operation)
    {
        const std::exception_ptr pointer = std::current_exception();
        if (pointer)
        {
            try
            {
                std::rethrow_exception(pointer);
            }
            catch (const std::exception& exception)
            {
                return exception_error(operation, exception);
            }
            catch (const Error& error)
            {
                return error;
            }
            catch (const std::string& text)
            {
                return make_error(operation, "exception_error", text,
                    {{"exception_type", "std::string"}, {"value", text_payload(text)}});
            }
            catch (const char* text)
            {
                return make_error(operation, "exception_error",
                    text == nullptr ? "Null exception value" : text,
                    {{"exception_type", "const char*"},
                     {"value", text == nullptr ? nlohmann::json(nullptr) : text_payload(text)}});
            }
            catch (...)
            {
                return make_error(operation, "exception_error",
                    "Non-standard exception", {{"exception_type", "unknown"}});
            }
        }
        return make_error(operation, "exception_error", "No active exception");
    }

    Error dependency_error(
        std::string_view operation,
        std::string_view message,
        std::vector<Error>&& causes,
        nlohmann::json&& details)
    {
        Error error = make_error(operation, "dependency_error", message, std::move(details));
        error.causes = std::move(causes);
        return error;
    }

    void append_error(std::optional<Error>& target, Error&& error)
    {
        if (!target)
        {
            target = std::move(error);
            return;
        }
        // Independent failures are siblings; an existing dependency retains its children.
        if (target->source == "tool_runtime" && target->operation == "run_process"
            && target->type == "dependency_error")
        {
            target->causes.push_back(std::move(error));
            return;
        }
        std::vector<Error> causes;
        causes.push_back(std::move(*target));
        causes.push_back(std::move(error));
        target = dependency_error("run_process", "Multiple process operations failed", std::move(causes));
    }

    Result<nlohmann::json> parse_json(
        std::string_view input,
        std::string_view operation,
        nlohmann::json&& details)
    {
        try
        {
            return Result<nlohmann::json>::success(nlohmann::json::parse(input));
        }
        catch (const std::exception& exception)
        {
            details["input"] = text_payload(input);
            return Result<nlohmann::json>::failure(
                exception_error(operation, exception, std::move(details)));
        }
        catch (...)
        {
            Error error = current_exception_error(operation);
            details["input"] = text_payload(input);
            error.data.push_back(std::move(details));
            return Result<nlohmann::json>::failure(std::move(error));
        }
    }

    Error adapt_error(
        const nlohmann::json& input,
        std::string_view source,
        std::string_view operation)
    {
        auto decoded = deserialize_error(input);
        if (decoded.value)
            return std::move(*decoded.value);

        Error error{std::string(source), std::string(operation), "tool_error",
            "Tool returned an error", {}, {}};
        if (!input.is_object())
        {
            if (input.is_string())
                error.message = input.get<std::string>();
            error.data.push_back(input);
            return error;
        }

        nlohmann::json extra = nlohmann::json::object();
        for (const auto& [key, value] : input.items())
        {
            if (key == "source" && value.is_string())
                error.source = value.get<std::string>();
            else if (key == "operation" && value.is_string())
                error.operation = value.get<std::string>();
            else if (key == "type" && value.is_string())
                error.type = value.get<std::string>();
            else if (key == "message" && value.is_string())
                error.message = value.get<std::string>();
            else if (key == "data")
            {
                if (value.is_array())
                    error.data = value.get<nlohmann::json::array_t>();
                else
                    error.data.push_back(value);
            }
            else if (key == "causes" || key == "cause" || key == "inner_error"
                || key == "source_error" || key == "raw_error")
            {
                if (value.is_array())
                {
                    for (const auto& cause : value)
                        error.causes.push_back(adapt_error(cause, source, operation));
                }
                else if (!value.is_null())
                    error.causes.push_back(adapt_error(value, source, operation));
            }
            else
                extra[key] = value;
        }
        if (!extra.empty())
            error.data.push_back(std::move(extra));
        return error;
    }
}
