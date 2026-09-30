#include "emit_usage_parser.h"
#include "schema.h"
#include "../src/error/capture.h"

#include <cerrno>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <system_error>

namespace
{
    using Json = nlohmann::ordered_json;

    provider::Error file_error(
        std::string_view operation, std::string_view api,
        const char* path, int native_error, std::ios::iostate state)
    {
        nlohmann::json details = {
            {"api", api}, {"path", path}, {"stream_state", static_cast<int>(state)}};
        if (native_error != 0)
        {
            details["code"] = native_error;
            details["category"] = "errno";
        }
        return provider::error_detail::make_error(
            operation, "system_error",
            native_error != 0
                ? std::error_code(native_error, std::generic_category()).message()
                : "File stream reports a failure",
            {std::move(details)});
    }

    provider::Result<Json> read_json(const char* path)
    {
        std::string contents;
        try
        {
            errno = 0;
            std::ifstream input(path);
            const int open_error = errno;
            if (!input)
            {
                return provider::Result<Json>::failure(
                    file_error("read_schema", "std::ifstream::open",
                        path, open_error, input.rdstate()));
            }
            errno = 0;
            contents.assign(
                (std::istreambuf_iterator<char>(input)),
                std::istreambuf_iterator<char>{});
            const int read_error = errno;
            if (input.bad() || read_error != 0)
            {
                return provider::Result<Json>::failure(
                    file_error("read_schema", "std::istream::read",
                        path, read_error, input.rdstate()));
            }
            return provider::Result<Json>::success(Json::parse(contents));
        }
        catch (...)
        {
            return provider::Result<Json>::failure(
                provider::error_detail::capture_exception(
                    std::current_exception(), "read_schema",
                    {{{"path", path}, {"contents", contents}}}));
        }
    }

    provider::Result<void> write_file(const char* path, const std::string& content)
    {
        try
        {
            errno = 0;
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            const int open_error = errno;
            if (!output)
            {
                return provider::Result<void>::failure(
                    file_error("write_generated_file", "std::ofstream::open",
                        path, open_error, output.rdstate()));
            }
            errno = 0;
            output << content;
            output.flush();
            const int write_error = errno;
            if (!output)
            {
                return provider::Result<void>::failure(
                    file_error("write_generated_file", "std::ostream::write",
                        path, write_error, output.rdstate()));
            }
            errno = 0;
            output.close();
            const int close_error = errno;
            if (!output)
            {
                return provider::Result<void>::failure(
                    file_error("write_generated_file", "std::ofstream::close",
                        path, close_error, output.rdstate()));
            }
            return provider::Result<void>::success();
        }
        catch (...)
        {
            return provider::Result<void>::failure(
                provider::error_detail::capture_exception(
                    std::current_exception(), "write_generated_file", {{{"path", path}}}));
        }
    }

    int report(const provider::Error& error, int status = 1)
    {
        auto encoded = provider::serialize_error(error);
        if (encoded)
        {
            std::cerr << encoded.value->dump() << '\n';
        }
        return status;
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

        output << "        UsageState>;\n}\n";
        return output.str();
    }

    std::string generate_source(const Json& schema)
    {
        std::ostringstream output;
        output << "// Generated from the provider schema. Do not edit.\n"
               << "#include <provider>\n"
               << "#include \"request/requests.h\"\n"
               << "#include \"error/capture.h\"\n\n"
               << "namespace provider\n{\n"
               << "    Result<Provider> provider_from_name(const std::string& name)\n"
               << "    {\n        try\n        {\n";
        for (const Json& entry : schema.at("providers"))
        {
            const std::string id = entry.at("id").get<std::string>();
            output << "            if (name == " << Json(id).dump() << ")\n"
                   << "                return Result<Provider>::success(Provider::"
                   << id << ");\n";
        }
        output << "            return Result<Provider>::failure(error_detail::make_error(\n"
               << "                \"provider_from_name\", \"invalid_argument\",\n"
               << "                \"Unsupported provider\", {{{\"provider\", name}}}));\n"
               << "        }\n        catch (...)\n        {\n"
               << "            return Result<Provider>::failure(error_detail::capture_exception(\n"
               << "                std::current_exception(), \"provider_from_name\"));\n"
               << "        }\n    }\n\n"
               << "    Result<nlohmann::json> prepare_request_body(\n"
               << "        Provider selected, const nlohmann::json& body)\n"
               << "    {\n        try\n        {\n"
               << "            nlohmann::json prepared = body;\n"
               << "            switch (selected)\n            {\n";
        for (const Json& entry : schema.at("providers"))
        {
            output << "                case Provider::"
                   << entry.at("id").get<std::string>() << ":\n";
            if (entry.contains("stream_request_options"))
            {
                output << "                    if (prepared.value(\"stream\", false))\n"
                       << "                        prepared.merge_patch(nlohmann::json::parse("
                       << Json(entry.at("stream_request_options").dump()).dump()
                       << "));\n";
            }
            output << "                    return Result<nlohmann::json>::success(std::move(prepared));\n";
        }
        output << "            }\n"
               << "            return Result<nlohmann::json>::failure(error_detail::make_error(\n"
               << "                \"request\", \"invalid_argument\", \"Unsupported provider\",\n"
               << "                {{{\"provider\", static_cast<int>(selected)}}}));\n"
               << "        }\n        catch (...)\n        {\n"
               << "            return Result<nlohmann::json>::failure(error_detail::capture_exception(\n"
               << "                std::current_exception(), \"request\", {{{\"body\", body}}}));\n"
               << "        }\n    }\n\n";
        emit_usage_parser(output, schema);
        output << "}\n";
        return output.str();
    }

}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4)
        {
            return report(provider::error_detail::make_error(
                "generate_types", "invalid_argument",
                "Expected schema, output header and output source paths",
                {{{"argc", argc}}}), 2);
        }
        auto schema = read_json(argv[1]);
        if (!schema)
        {
            return report(*schema.error);
        }
        auto validation = validate_schema(*schema.value);
        if (!validation)
        {
            return report(*validation.error);
        }
        auto header = write_file(argv[2], generate_header(*schema.value));
        if (!header)
        {
            return report(*header.error);
        }
        auto source = write_file(argv[3], generate_source(*schema.value));
        if (!source)
        {
            return report(*source.error);
        }
        return 0;
    }
    catch (...)
    {
        return report(provider::error_detail::capture_exception(
            std::current_exception(), "generate_types"));
    }
}
