#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    using Json = nlohmann::ordered_json;

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

    struct UsageSchema
    {
        std::string usage_type;
        std::map<std::string, std::string> scalar_fields;
    };

    std::map<std::string, UsageSchema> provider_schemas(
        const Json& provider_types)
    {
        std::map<std::string, UsageSchema> result;

        for (const Json& provider : provider_types.at("providers"))
        {
            UsageSchema schema;
            schema.usage_type = provider.at("usage_type").get<std::string>();

            for (const Json& field : provider.at("fields"))
            {
                if (!field.contains("fields"))
                {
                    schema.scalar_fields.emplace(
                        field.at("name").get<std::string>(),
                        field.at("type").get<std::string>());
                }
            }

            result.emplace(
                provider.at("id").get<std::string>(),
                std::move(schema));
        }

        return result;
    }

    void validate(
        const Json& formulas,
        const Json& provider_types)
    {
        if (!formulas.is_object() ||
            !formulas.contains("providers") ||
            !formulas.at("providers").is_array())
        {
            throw std::runtime_error(
                "context_usage.json: providers must be an array");
        }

        const auto schemas = provider_schemas(provider_types);
        std::set<std::string> seen;

        for (const Json& entry : formulas.at("providers"))
        {
            const std::string id = entry.at("id").get<std::string>();
            const auto schema = schemas.find(id);

            if (schema == schemas.end())
            {
                throw std::runtime_error(
                    "context_usage.json: unknown provider: " + id);
            }

            if (!seen.insert(id).second)
            {
                throw std::runtime_error(
                    "context_usage.json: duplicate provider: " + id);
            }

            const std::string usage_type =
                entry.at("usage_type").get<std::string>();
            if (usage_type != schema->second.usage_type)
            {
                throw std::runtime_error(
                    "context_usage.json: usage_type mismatch for " + id);
            }

            const Json& formula = entry.at("formula");
            if (formula.at("operation").get<std::string>() != "sum")
            {
                throw std::runtime_error(
                    "context_usage.json: unsupported operation for " + id);
            }

            const Json& fields = formula.at("fields");
            if (!fields.is_array() || fields.empty())
            {
                throw std::runtime_error(
                    "context_usage.json: formula fields must be nonempty for " + id);
            }

            std::set<std::string> formula_fields;
            for (const Json& field_json : fields)
            {
                const std::string field = field_json.get<std::string>();
                if (!formula_fields.insert(field).second)
                {
                    throw std::runtime_error(
                        "context_usage.json: duplicate formula field " +
                        id + "." + field);
                }

                const auto scalar = schema->second.scalar_fields.find(field);
                if (scalar == schema->second.scalar_fields.end())
                {
                    throw std::runtime_error(
                        "context_usage.json: field not found in usage struct: " +
                        id + "." + field);
                }

                if (scalar->second != "uint64")
                {
                    throw std::runtime_error(
                        "context_usage.json: formula field must be uint64: " +
                        id + "." + field);
                }
            }
        }

        if (seen.size() != schemas.size())
        {
            for (const auto& [id, schema] : schemas)
            {
                (void)schema;
                if (!seen.contains(id))
                {
                    throw std::runtime_error(
                        "context_usage.json: missing provider: " + id);
                }
            }
        }
    }

    std::string generate(const Json& formulas)
    {
        std::ostringstream output;
        output
            << "// Generated from context_usage.json. Do not edit.\n"
            << "#pragma once\n\n"
            << "#include <provider_types.generated.h>\n\n"
            << "#include <cstdint>\n"
            << "#include <limits>\n"
            << "#include <stdexcept>\n"
            << "#include <type_traits>\n"
            << "#include <variant>\n\n"
            << "namespace provider\n"
            << "{\n"
            << "    namespace context_usage_detail\n"
            << "    {\n"
            << "        inline std::uint64_t checked_add(\n"
            << "            std::uint64_t left,\n"
            << "            std::uint64_t right)\n"
            << "        {\n"
            << "            if (right > (std::numeric_limits<std::uint64_t>::max)() - left)\n"
            << "            {\n"
            << "                throw std::overflow_error(\"provider: context usage overflow\");\n"
            << "            }\n"
            << "            return left + right;\n"
            << "        }\n\n";

        for (const Json& entry : formulas.at("providers"))
        {
            const std::string type = entry.at("usage_type").get<std::string>();
            output << "        inline std::uint64_t calculate(const "
                   << type << "& usage)\n"
                   << "        {\n"
                   << "            std::uint64_t total = 0;\n";

            for (const Json& field : entry.at("formula").at("fields"))
            {
                output << "            total = checked_add(total, usage."
                       << field.get<std::string>() << ");\n";
            }

            output << "            return total;\n"
                   << "        }\n\n";
        }

        output
            << "    }\n\n"
            << "    inline std::uint64_t context_usage_generated(\n"
            << "        const RequestUsage& usage)\n"
            << "    {\n"
            << "        return std::visit(\n"
            << "            [](const auto& value) -> std::uint64_t\n"
            << "            {\n"
            << "                using T = std::decay_t<decltype(value)>;\n"
            << "                if constexpr (std::is_same_v<T, UsageState>)\n"
            << "                {\n"
            << "                    throw std::runtime_error(\n"
            << "                        \"provider: context usage unavailable\");\n"
            << "                }\n"
            << "                else\n"
            << "                {\n"
            << "                    return context_usage_detail::calculate(value);\n"
            << "                }\n"
            << "            },\n"
            << "            usage);\n"
            << "    }\n"
            << "}\n";

        return output.str();
    }
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr
            << "usage: generate_context_usage "
               "<context_usage.json> <provider_types.json> <output.h>\n";
        return 2;
    }

    try
    {
        const Json formulas = read_json(argv[1]);
        const Json provider_types = read_json(argv[2]);
        validate(formulas, provider_types);

        const std::string header = generate(formulas);
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
