#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace sandbox
{
    struct Error
    {
        std::string source;
        std::string operation;
        std::string type;
        std::string message;
        nlohmann::json::array_t data;
        std::vector<Error> causes;
    };

    template<class T>
    class Result final
    {
    public:
        std::optional<T> value;
        std::optional<Error> error;

        [[nodiscard]] static Result success(T&& value)
        {
            return Result(std::move(value), std::nullopt);
        }

        [[nodiscard]] static Result failure(Error&& error)
        {
            return Result(std::nullopt, std::move(error));
        }

    private:
        Result(std::optional<T>&& value, std::optional<Error>&& error)
            : value(std::move(value)), error(std::move(error))
        {
        }
    };

    template<>
    class Result<void> final
    {
    public:
        std::optional<std::monostate> value;
        std::optional<Error> error;

        [[nodiscard]] static Result success()
        {
            return Result(std::monostate{}, std::nullopt);
        }

        [[nodiscard]] static Result failure(Error&& error)
        {
            return Result(std::nullopt, std::move(error));
        }

    private:
        Result(std::optional<std::monostate>&& value, std::optional<Error>&& error)
            : value(std::move(value)), error(std::move(error))
        {
        }
    };

    void to_json(nlohmann::json& json, const Error& error);
    [[nodiscard]] Result<nlohmann::json> serialize_error(const Error& error);
    [[nodiscard]] Result<Error> deserialize_error(const nlohmann::json& json);
}
