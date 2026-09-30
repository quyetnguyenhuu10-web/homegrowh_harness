#include "schema.h"
#include "../src/error/capture.h"

#include <set>

namespace
{
    using Json = nlohmann::ordered_json;

    provider::Result<void> invalid(
        const std::string& location,
        std::string_view message,
        const Json& payload)
    {
        return provider::Result<void>::failure(
            provider::error_detail::make_error(
                "validate_schema", "schema_error", message,
                {{{"path", location}, {"payload", payload}}}));
    }

    bool valid_identifier(const std::string& value)
    {
        const auto letter = [](char ch)
        {
            return (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') || ch == '_';
        };
        if (value.empty() || !letter(value.front()))
        {
            return false;
        }
        for (const char ch : value)
        {
            if (!letter(ch) && !(ch >= '0' && ch <= '9'))
            {
                return false;
            }
        }
        return true;
    }

    provider::Result<void> validate_fields(
        const Json& fields, const std::string& location)
    {
        if (!fields.is_array() || fields.empty())
        {
            return invalid(location, "Fields must be a nonempty array", fields);
        }
        std::set<std::string> names;
        std::set<std::string> nested_types;
        for (const Json& field : fields)
        {
            if (!field.is_object())
            {
                return invalid(location, "Field must be an object", field);
            }
            const std::string name = field.at("name").get<std::string>();
            const std::string type = field.at("type").get<std::string>();
            if (!valid_identifier(name) || !valid_identifier(type))
            {
                return invalid(location, "Invalid C++ identifier", field);
            }
            if (field.value("json_key", name).empty())
            {
                return invalid(location, "JSON key must not be empty", field);
            }
            field.value("optional", false);
            if (!names.insert(name).second)
            {
                return invalid(location, "Duplicate field name", field);
            }
            if (field.contains("fields"))
            {
                if (type == "uint64" || type == "double"
                    || !nested_types.insert(type).second)
                {
                    return invalid(location, "Invalid nested struct type", field);
                }
                auto nested = validate_fields(field.at("fields"), location + "." + name);
                if (!nested)
                {
                    return nested;
                }
            }
            else if (type != "uint64" && type != "double")
            {
                return invalid(location, "Unsupported scalar type", field);
            }
        }
        return provider::Result<void>::success();
    }
}

provider::Result<void> validate_schema(const Json& schema)
{
    try
    {
        if (!schema.is_object() || !schema.contains("providers")
            || !schema.at("providers").is_array() || schema.at("providers").empty())
        {
            return invalid("providers", "Providers must be a nonempty array", schema);
        }
        std::set<std::string> ids;
        std::set<std::string> usage_types;
        for (const Json& entry : schema.at("providers"))
        {
            if (!entry.is_object())
            {
                return invalid("providers", "Provider must be an object", entry);
            }
            const std::string id = entry.at("id").get<std::string>();
            const std::string type = entry.at("usage_type").get<std::string>();
            if (!valid_identifier(id) || !valid_identifier(type))
            {
                return invalid("providers", "Invalid C++ identifier", entry);
            }
            if (entry.at("usage_key").get<std::string>().empty())
            {
                return invalid(id, "Usage key must not be empty", entry);
            }
            if (!ids.insert(id).second || !usage_types.insert(type).second)
            {
                return invalid(id, "Duplicate provider id or usage type", entry);
            }
            if (entry.contains("stream_request_options")
                && !entry.at("stream_request_options").is_object())
            {
                return invalid(id, "Stream request options must be an object", entry);
            }
            auto fields = validate_fields(entry.at("fields"), type);
            if (!fields)
            {
                return fields;
            }
        }
        return provider::Result<void>::success();
    }
    catch (...)
    {
        return provider::Result<void>::failure(
            provider::error_detail::capture_exception(
                std::current_exception(), "validate_schema", {{{"schema", schema}}}));
    }
}
