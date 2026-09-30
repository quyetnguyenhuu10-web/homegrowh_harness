#include "error.h"
#include "capture.h"

#include <iterator>
#include <iterator>
#include <string_view>

namespace provider
{
    namespace
    {
        nlohmann::json encode(const Error& error)
        {
            nlohmann::json causes = nlohmann::json::array();
            for (const Error& cause : error.causes)
            {
                causes.push_back(encode(cause));
            }
            return {
                {"source", error.source},
                {"operation", error.operation},
                {"type", error.type},
                {"message", error.message},
                {"data", error.data},
                {"causes", std::move(causes)}};
        }

        Result<Error> decode(
            const nlohmann::json& payload,
            const std::string& path)
        {
            const auto invalid = [&](std::string_view expected)
            {
                return Result<Error>::failure(error_detail::make_error(
                    "deserialize_error", "protocol_error",
                    "Invalid error schema",
                    {{{"path", path}, {"expected", expected},
                      {"payload", payload}}}));
            };

            if (!payload.is_object())
            {
                return invalid("object");
            }

            Error validation = error_detail::make_error(
                "deserialize_error", "protocol_error", "Invalid error schema",
                {{{"path", path}, {"payload", payload}}});
            constexpr std::string_view fields[] = {
                "source", "operation", "type", "message", "data", "causes"};
            for (const std::string_view field : fields)
            {
                const auto found = payload.find(std::string(field));
                const bool array = field == "data" || field == "causes";
                if (found == payload.end()
                    || (array ? !found->is_array() : !found->is_string()))
                {
                    validation.causes.push_back(error_detail::make_error(
                        "deserialize_error", "protocol_error",
                        "Invalid error field",
                        {{{"path", path + "/" + std::string(field)},
                          {"expected", array ? "array" : "string"},
                          {"present", found != payload.end()}}}));
                }
            }
            if (payload.size() != std::size(fields))
            {
                validation.causes.push_back(error_detail::make_error(
                    "deserialize_error", "protocol_error",
                    "Error envelope must contain exactly six fields",
                    {{{"path", path}, {"fields", fields}}}));
            }
            if (!validation.causes.empty())
            {
                return Result<Error>::failure(std::move(validation));
            }

            Error error{
                payload.at("source").get<std::string>(),
                payload.at("operation").get<std::string>(),
                payload.at("type").get<std::string>(),
                payload.at("message").get<std::string>(),
                payload.at("data").get<nlohmann::json::array_t>(), {}};
            std::size_t index = 0;
            for (const nlohmann::json& cause : payload.at("causes"))
            {
                auto decoded = decode(
                    cause, path + "/causes/" + std::to_string(index++));
                if (!decoded)
                {
                    validation.causes.push_back(std::move(*decoded.error));
                }
                else
                {
                    error.causes.push_back(std::move(*decoded.value));
                }
            }
            if (!validation.causes.empty())
            {
                return Result<Error>::failure(std::move(validation));
            }
            return Result<Error>::success(std::move(error));
        }
    }

    Result<nlohmann::json> serialize_error(const Error& error)
    {
        try
        {
            return Result<nlohmann::json>::success(encode(error));
        }
        catch (...)
        {
            return Result<nlohmann::json>::failure(
                error_detail::capture_exception(
                    std::current_exception(), "serialize_error"));
        }
    }

    Result<Error> deserialize_error(const nlohmann::json& payload)
    {
        try
        {
            return decode(payload, "");
        }
        catch (...)
        {
            return Result<Error>::failure(error_detail::capture_exception(
                std::current_exception(), "deserialize_error",
                {{{"payload", payload}}}));
        }
    }
}
