#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace tool_runtime
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
            return {std::move(value), std::nullopt};
        }

        static Result failure(Error&& error)
        {
            return {std::nullopt, std::move(error)};
        }
    };

    void to_json(nlohmann::json& json, const Error& error);
    Result<Error> deserialize_error(const nlohmann::json& json);
}
