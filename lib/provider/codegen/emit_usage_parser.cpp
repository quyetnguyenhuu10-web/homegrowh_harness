#include "emit_usage_parser.h"

#include <ostream>
#include <string>

namespace
{
    using Json = nlohmann::ordered_json;

    std::string literal(const std::string& value)
    {
        return Json(value).dump();
    }

    std::string field_key(const Json& field)
    {
        return field.value("json_key", field.at("name").get<std::string>());
    }

    std::string cpp_type(const std::string& type)
    {
        return type == "uint64" ? "std::uint64_t" : type;
    }

    void emit_struct_parser(
        std::ostream& output,
        const std::string& type,
        const Json& fields,
        const std::string& function_name)
    {
        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            const Json& field = fields.at(index);

            if (field.contains("fields"))
            {
                emit_struct_parser(
                    output,
                    type + "::" + field.at("type").get<std::string>(),
                    field.at("fields"),
                    function_name + "_" + std::to_string(index));
            }
        }

        output << "        inline " << type << " " << function_name
               << "(const nlohmann::json& object)\n"
               << "        {\n"
               << "            " << type << " result;\n";

        for (std::size_t index = 0; index < fields.size(); ++index)
        {
            const Json& field = fields.at(index);
            const std::string name = field.at("name").get<std::string>();
            const std::string key = literal(field_key(field));
            const bool optional = field.value("optional", false);

            if (field.contains("fields"))
            {
                const std::string nested_parser =
                    function_name + "_" + std::to_string(index);

                if (optional)
                {
                    output << "            if (const auto found = object.find("
                           << key << "); found != object.end() && "
                           << "!found->is_null())\n"
                           << "            {\n"
                           << "                result." << name << " = "
                           << nested_parser << "(*found);\n"
                           << "            }\n";
                }
                else
                {
                    output << "            result." << name << " = "
                           << nested_parser << "(object.at(" << key
                           << "));\n";
                }
            }
            else if (optional)
            {
                output << "            result." << name
                       << " = optional_value<"
                       << cpp_type(field.at("type").get<std::string>())
                       << ">(object, " << key << ");\n";
            }
            else
            {
                output << "            result." << name << " = object.at("
                       << key << ").get<"
                       << cpp_type(field.at("type").get<std::string>())
                       << ">();\n";
            }
        }

        output << "            return result;\n"
               << "        }\n\n";
    }
}

void emit_usage_parser(std::ostream& output, const Json& schema)
{
    const Json& providers = schema.at("providers");

    output << "    namespace usage_detail\n"
           << "    {\n"
           << "        template <typename T>\n"
           << "        inline std::optional<T> optional_value(\n"
           << "            const nlohmann::json& object,\n"
           << "            const char* key)\n"
           << "        {\n"
           << "            const auto found = object.find(key);\n"
           << "            if (found == object.end() || found->is_null())\n"
           << "            {\n"
           << "                return std::nullopt;\n"
           << "            }\n"
           << "            return found->get<T>();\n"
           << "        }\n\n";

    for (std::size_t index = 0; index < providers.size(); ++index)
    {
        const Json& provider = providers.at(index);
        emit_struct_parser(
            output,
            provider.at("usage_type").get<std::string>(),
            provider.at("fields"),
            "parse_struct_" + std::to_string(index));
    }


    output << R"(
    }

    Result<std::optional<nlohmann::json>> usage_from_event(
        Provider selected, const nlohmann::json& event)
    {
        try
        {
            const char* key = nullptr;
            switch (selected)
            {
)";
    for (const Json& entry : providers)
    {
        output << "                case Provider::"
               << entry.at("id").get<std::string>() << ":\n"
               << "                    key = "
               << literal(entry.at("usage_key").get<std::string>()) << ";\n"
               << "                    break;\n";
    }
    output << R"(
            }
            if (key == nullptr)
            {
                return Result<std::optional<nlohmann::json>>::failure(
                    error_detail::make_error("usage_from_event", "invalid_argument",
                        "Unsupported provider",
                        {{{"provider", static_cast<int>(selected)}}}));
            }
            if (!event.is_object())
            {
                return Result<std::optional<nlohmann::json>>::failure(
                    error_detail::make_error("usage_from_event", "protocol_error",
                        "Provider event must be an object", {{{"event", event}}}));
            }
            const auto found = event.find(key);
            if (found == event.end() || found->is_null())
            {
                return Result<std::optional<nlohmann::json>>::success(std::nullopt);
            }
            return Result<std::optional<nlohmann::json>>::success(*found);
        }
        catch (...)
        {
            return Result<std::optional<nlohmann::json>>::failure(
                error_detail::capture_exception(std::current_exception(),
                    "usage_from_event", {{{"event", event}}}));
        }
    }

    Result<RequestUsage> parse_usage(
        Provider selected, const nlohmann::json& usage)
    {
        try
        {
            switch (selected)
            {
)";
    for (std::size_t index = 0; index < providers.size(); ++index)
    {
        output << "                case Provider::"
               << providers.at(index).at("id").get<std::string>() << ":\n"
               << "                    return Result<RequestUsage>::success(\n"
               << "                        RequestUsage{usage_detail::parse_struct_"
               << index << "(usage)});\n";
    }
    output << R"(
            }
            return Result<RequestUsage>::failure(error_detail::make_error(
                "parse_usage", "invalid_argument", "Unsupported provider",
                {{{"provider", static_cast<int>(selected)}}}));
        }
        catch (...)
        {
            return Result<RequestUsage>::failure(error_detail::capture_exception(
                std::current_exception(), "parse_usage",
                {{{"provider", static_cast<int>(selected)}, {"usage", usage}}}));
        }
    }
)";
}
