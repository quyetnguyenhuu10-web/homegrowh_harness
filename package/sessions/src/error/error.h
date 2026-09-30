#pragma once

#include <session>

#include <exception>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <typeinfo>

namespace sessions::detail
{
    template <typename Destination, typename Original>
    Destination convert_error(Original&& error)
    {
        std::vector<Destination> causes;
        causes.reserve(error.causes.size());
        for (auto& cause : error.causes)
            causes.push_back(convert_error<Destination>(std::move(cause)));
        return {std::move(error.source), std::move(error.operation),
            std::move(error.type), std::move(error.message),
            std::move(error.data), std::move(causes)};
    }

    inline Error dependency_error(
        const char* operation,
        const char* message,
        Error&& cause)
    {
        std::vector<Error> causes;
        causes.push_back(std::move(cause));
        return {"sessions", operation, "dependency_error", message, {}, std::move(causes)};
    }

    inline Error exception_error(
        std::string_view operation, std::exception_ptr exception,
        std::string_view source = "sessions");

    // Let the boundary adapter preserve its module-specific exceptions inside causes.
    template <typename ConvertNested>
    Error exception_error(
        std::string_view operation, const std::exception& exception,
        std::string_view source, ConvertNested&& convert_nested)
    {
        if (const auto* structured = dynamic_cast<const ErrorException*>(&exception))
            return Error(structured->error());

        std::string type = "exception";
        nlohmann::json data = {{"exception_type", typeid(exception).name()}};
        if (const auto* json = dynamic_cast<const nlohmann::json::exception*>(&exception))
        {
            type = "json_error";
            data["id"] = json->id;
            if (const auto* parse = dynamic_cast<const nlohmann::json::parse_error*>(json))
                data["byte"] = parse->byte;
        }
        if (const auto* system = dynamic_cast<const std::system_error*>(&exception))
        {
            type = "system_error";
            data["code"] = system->code().value();
            data["category"] = system->code().category().name();
        }
        if (const auto* filesystem =
                dynamic_cast<const std::filesystem::filesystem_error*>(&exception))
        {
            type = "filesystem_error";
            const auto put_path = [&data](const char* key, const std::filesystem::path& path)
            {
                if (!path.empty())
                {
                    const auto utf8 = path.u8string();
                    data[key] = std::string(utf8.begin(), utf8.end());
                }
            };
            put_path("path1", filesystem->path1());
            put_path("path2", filesystem->path2());
        }

        std::vector<Error> causes;
        if (const auto* nested = dynamic_cast<const std::nested_exception*>(&exception);
            nested != nullptr && nested->nested_ptr() != nullptr)
        {
            causes.push_back(convert_nested(nested->nested_ptr()));
        }
        return {std::string(source), std::string(operation), std::move(type),
            exception.what(), {std::move(data)}, std::move(causes)};
    }

    inline Error exception_error(
        std::string_view operation, const std::exception& exception,
        std::string_view source = "sessions")
    {
        return exception_error(operation, exception, source,
            [operation, source](std::exception_ptr nested)
            {
                return exception_error(operation, std::move(nested), source);
            });
    }

    inline Error exception_error(
        std::string_view operation, std::exception_ptr exception,
        std::string_view source)
    {
        if (exception == nullptr)
            return {std::string(source), std::string(operation), "missing_exception", "", {}, {}};

        try
        {
            std::rethrow_exception(exception);
        }
        catch (const std::exception& error)
        {
            return exception_error(operation, error, source);
        }
        catch (...)
        {
            return {std::string(source), std::string(operation), "unknown_exception", "", {}, {}};
        }
    }
}
