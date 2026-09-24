#include <nlohmann/json.hpp>

#include "emit_usage_parser.h"

#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    using Json = nlohmann::ordered_json;

    bool valid_identifier(const std::string& value)
    {
        if (value.empty())
        {
            return false;
        }

        const auto letter = [](char ch)
        {
            return (ch >= 'a' && ch <= 'z') ||
                (ch >= 'A' && ch <= 'Z') ||
                ch == '_';
        };

        const auto digit = [](char ch)
        {
            return ch >= '0' && ch <= '9';
        };

        if (!letter(value.front()))
        {
            return false;
        }

        for (const char ch : value)
        {
            if (!letter(ch) && !digit(ch))
            {
                return false;
            }
        }

        return true;
    }

    std::string identifier(
        const Json& object,
        const char* key,
        const std::string& location)
    {
        const std::string value = object.at(key).get<std::string>();

        if (!valid_identifier(value))
        {
            throw std::runtime_error(
                location + ": invalid C++ identifier in " + key + ": " + value);
        }

        return value;
    }

    Json read_json(const char* path)
    {
        std::ifstream input(path);

        if (!input)
        {
            throw std::runtime_error(
                std::string("cannot open JSON file: ") + path);
        }

        Json result;
        input >> result;
        return result;
    }

    void validate_fields(
        const Json& fields,
        const std::string& location)
    {
        if (!fields.is_array() || fields.empty())
        {
            throw std::runtime_error(
                location + ": fields must be a nonempty array");
        }

        std::set<std::string> names;
        std::set<std::string> nested_types;

        for (const Json& field : fields)
        {
            if (!field.is_object())
            {
                throw std::runtime_error(
                    location + ": field must be an object");
            }

            const std::string name = identifier(field, "name", location);
            const std::string type = identifier(field, "type", location);
            const std::string json_key = field.value("json_key", name);
            field.value("optional", false);

            if (json_key.empty())
            {
                throw std::runtime_error(
                    location + ": json_key must not be empty: " + name);
            }

            if (!names.insert(name).second)
            {
                throw std::runtime_error(
                    location + ": duplicate field: " + name);
            }

            if (field.contains("fields"))
            {
                if (type == "uint64" || type == "double")
                {
                    throw std::runtime_error(
                        location + ": nested struct needs a struct type: " + name);
                }

                if (!nested_types.insert(type).second)
                {
                    throw std::runtime_error(
                        location + ": duplicate nested type: " + type);
                }

                validate_fields(field.at("fields"), location + "." + name);
            }
            else if (type != "uint64" && type != "double")
            {
                throw std::runtime_error(
                    location + ": unsupported scalar type: " + type);
            }
        }
    }

    void validate_schema(
        const Json& schema,
        const Json& catalog)
    {
        if (!schema.is_object() ||
            !schema.contains("providers") ||
            !schema.at("providers").is_array() ||
            schema.at("providers").empty())
        {
            throw std::runtime_error(
                "provider_types.json: providers must be a nonempty array");
        }

        if (!catalog.is_object())
        {
            throw std::runtime_error(
                "catalog.json: expected an object of providers");
        }

        std::set<std::string> ids;
        std::set<std::string> usage_types;

        for (const Json& provider : schema.at("providers"))
        {
            if (!provider.is_object())
            {
                throw std::runtime_error(
                    "provider_types.json: provider must be an object");
            }

            const std::string id =
                identifier(provider, "id", "provider_types.json");
            const std::string usage_type =
                identifier(provider, "usage_type", id);
            const std::string usage_key =
                provider.at("usage_key").get<std::string>();

            if (usage_key.empty())
            {
                throw std::runtime_error(
                    "provider_types.json: usage_key must not be empty: " + id);
            }

            if (!ids.insert(id).second)
            {
                throw std::runtime_error(
                    "provider_types.json: duplicate provider id: " + id);
            }

            if (!usage_types.insert(usage_type).second)
            {
                throw std::runtime_error(
                    "provider_types.json: duplicate usage type: " + usage_type);
            }

            validate_fields(provider.at("fields"), usage_type);

            if (!catalog.contains(id))
            {
                throw std::runtime_error(
                    "provider_types.json: provider missing from catalog.json: " + id);
            }
        }

        for (const auto& [id, value] : catalog.items())
        {
            (void)value;

            if (!ids.contains(id))
            {
                throw std::runtime_error(
                    "catalog.json: provider missing from provider_types.json: " + id);
            }
        }
    }

    void indent(std::ostream& output, int spaces)
    {
        output << std::string(static_cast<std::size_t>(spaces), ' ');
    }

    std::string cpp_type(const Json& field)
    {
        const std::string type = field.at("type").get<std::string>();

        if (type == "uint64")
        {
            return "std::uint64_t";
        }

        return type;
    }

    void emit_struct(
        std::ostream& output,
        const std::string& name,
        const Json& fields,
        int spaces)
    {
        indent(output, spaces);
        output << "struct " << name << "\n";
        indent(output, spaces);
        output << "{\n";

        for (const Json& field : fields)
        {
            if (field.contains("fields"))
            {
                emit_struct(
                    output,
                    field.at("type").get<std::string>(),
                    field.at("fields"),
                    spaces + 4);
                output << "\n";
            }
        }

        for (const Json& field : fields)
        {
            const std::string type = cpp_type(field);
            const bool optional = field.value("optional", false);

            indent(output, spaces + 4);

            if (optional)
            {
                output << "std::optional<" << type << ">";
            }
            else
            {
                output << type;
            }

            output << " " << field.at("name").get<std::string>();

            if (!optional && type == "std::uint64_t")
            {
                output << " = 0";
            }
            else if (!optional && type == "double")
            {
                output << " = 0.0";
            }

            output << ";\n";
        }

        indent(output, spaces);
        output << "};\n";
    }

    std::string generate_header(const Json& schema)
    {
        std::ostringstream output;
        output << "// Generated from provider_types.json. Do not edit.\n"
               << "#pragma once\n\n"
               << "#include <cstdint>\n"
               << "#include <optional>\n"
               << "#include <stdexcept>\n"
               << "#include <string>\n"
               << "#include <variant>\n"
               << "#include <nlohmann/json.hpp>\n\n"
               << "namespace provider\n"
               << "{\n"
               << "    enum class Provider\n"
               << "    {\n";

        const Json& providers = schema.at("providers");

        for (std::size_t i = 0; i < providers.size(); ++i)
        {
            output << "        "
                   << providers.at(i).at("id").get<std::string>();

            if (i + 1 != providers.size())
            {
                output << ",";
            }

            output << "\n";
        }

        output << "    };\n\n";

        for (const Json& entry : providers)
        {
            emit_struct(
                output,
                entry.at("usage_type").get<std::string>(),
                entry.at("fields"),
                4);
            output << "\n";
        }

        output << "    enum class UsageState\n"
               << "    {\n"
               << "        unavailable\n"
               << "    };\n\n"
               << "    using RequestUsage = std::variant<\n";

        for (const Json& entry : providers)
        {
            output << "        "
                   << entry.at("usage_type").get<std::string>()
                   << ",\n";
        }

        output << "        UsageState>;\n\n"
               << "    inline Provider provider_from_name(\n"
               << "        const std::string& provider_name)\n"
               << "    {\n";

        for (const Json& entry : providers)
        {
            const std::string id = entry.at("id").get<std::string>();
            output << "        if (provider_name == \"" << id << "\")\n"
                   << "        {\n"
                   << "            return Provider::" << id << ";\n"
                   << "        }\n";
        }

        output << "\n"
               << "        throw std::runtime_error(\n"
               << "            \"unsupported provider in catalog: \" + provider_name);\n"
               << "    }\n\n";

        emit_usage_parser(output, schema);
        output << "}\n";

        return output.str();
    }
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr
            << "usage: generate_provider_types "
               "<provider_types.json> <catalog.json> <output.h>\n";
        return 2;
    }

    try
    {
        const Json schema = read_json(argv[1]);
        const Json catalog = read_json(argv[2]);
        validate_schema(schema, catalog);

        const std::string header = generate_header(schema);
        std::ofstream output(argv[3], std::ios::binary | std::ios::trunc);

        if (!output || !(output << header))
        {
            throw std::runtime_error(
                std::string("cannot write generated header: ") + argv[3]);
        }
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
