#include "../error_schema.h"

#include <array>
#include <string_view>

namespace sandbox
{
    namespace
    {
        Error invalid_schema(
            const nlohmann::json& payload,
            const std::string& path,
            std::string message)
        {
            return detail::make_error(
                "deserialize_error", "protocol_error", std::move(message),
                {{"path", path}, {"payload", payload}});
        }

        Result<Error> decode(
            const nlohmann::json& json,
            const std::string& path,
            const nlohmann::json& original)
        {
            constexpr std::array<std::string_view, 6> fields{
                "source", "operation", "type", "message", "data", "causes"};
            if (!json.is_object())
                return Result<Error>::failure(
                    invalid_schema(original, path, "Error must be an object"));
            if (json.size() != fields.size())
                return Result<Error>::failure(
                    invalid_schema(original, path, "Error must contain exactly six fields"));
            for (const auto field : fields)
            {
                if (!json.contains(field))
                    return Result<Error>::failure(invalid_schema(
                        original, path + "." + std::string(field), "Required error field is missing"));
            }
            for (std::size_t index = 0; index < 4; ++index)
            {
                const auto field = fields[index];
                if (!json.at(field).is_string())
                    return Result<Error>::failure(invalid_schema(
                        original, path + "." + std::string(field), "Error field must be a string"));
            }
            for (const auto field : {fields[4], fields[5]})
            {
                if (!json.at(field).is_array())
                    return Result<Error>::failure(invalid_schema(
                        original, path + "." + std::string(field), "Error field must be an array"));
            }

            Error error{
                json.at("source").get<std::string>(),
                json.at("operation").get<std::string>(),
                json.at("type").get<std::string>(),
                json.at("message").get<std::string>(),
                json.at("data").get<nlohmann::json::array_t>(),
                {},
            };
            const auto& causes = json.at("causes");
            error.causes.reserve(causes.size());
            for (std::size_t index = 0; index < causes.size(); ++index)
            {
                auto cause = decode(
                    causes[index], path + ".causes[" + std::to_string(index) + "]", original);
                if (cause.error)
                    return Result<Error>::failure(std::move(*cause.error));
                error.causes.push_back(std::move(*cause.value));
            }
            return Result<Error>::success(std::move(error));
        }
    }

    void to_json(nlohmann::json& json, const Error& error)
    {
        try
        {
            json = nlohmann::json{
                {"source", error.source},
                {"operation", error.operation},
                {"type", error.type},
                {"message", error.message},
                {"data", error.data},
                {"causes", error.causes},
            };
        }
        catch (...)
        {
            detail::throw_error(detail::capture_exception(
                "serialize_error", std::current_exception()));
        }
    }

    Result<nlohmann::json> serialize_error(const Error& error)
    {
        try
        {
            return Result<nlohmann::json>::success(nlohmann::json(error));
        }
        catch (...)
        {
            return Result<nlohmann::json>::failure(
                detail::capture_exception("serialize_error", std::current_exception()));
        }
    }

    Result<Error> deserialize_error(const nlohmann::json& json)
    {
        try
        {
            return decode(json, "$", json);
        }
        catch (...)
        {
            return Result<Error>::failure(detail::capture_exception(
                "deserialize_error", std::current_exception(), {{"payload", json}}));
        }
    }
}
