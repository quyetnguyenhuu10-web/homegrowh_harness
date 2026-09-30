#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

namespace provider
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

    template <typename T>
    struct Result
    {
        std::optional<T> value;
        std::optional<Error> error;

        static Result success(T&& value)
        {
            return Result{std::move(value), std::nullopt};
        }

        static Result failure(Error&& error)
        {
            return Result{std::nullopt, std::move(error)};
        }

        explicit operator bool() const noexcept
        {
            return value.has_value() && !error.has_value();
        }

    private:
        Result(std::optional<T>&& value, std::optional<Error>&& error)
            : value(std::move(value)), error(std::move(error))
        {
        }
    };

    template <>
    struct Result<void>
    {
        std::optional<std::monostate> value;
        std::optional<Error> error;

        static Result success()
        {
            return Result{std::monostate{}, std::nullopt};
        }

        static Result failure(Error&& error)
        {
            return Result{std::nullopt, std::move(error)};
        }

        explicit operator bool() const noexcept
        {
            return value.has_value() && !error.has_value();
        }

    private:
        Result(
            std::optional<std::monostate>&& value,
            std::optional<Error>&& error)
            : value(std::move(value)), error(std::move(error))
        {
        }
    };

    Result<nlohmann::json> serialize_error(const Error& error);
    Result<Error> deserialize_error(const nlohmann::json& payload);
}
