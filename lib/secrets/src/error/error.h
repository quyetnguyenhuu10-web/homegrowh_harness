#pragma once

#include <secrets>

#include <exception>
#include <system_error>
#include <typeinfo>
#include <utility>

namespace secrets::detail
{
    inline Error make_error(
        std::string operation,
        std::string type,
        std::string message,
        nlohmann::json::array_t&& data = {},
        std::vector<Error>&& causes = {})
    {
        return {
            "secrets",
            std::move(operation),
            std::move(type),
            std::move(message),
            std::move(data),
            std::move(causes),
        };
    }

    inline Error exception_error(
        const char* operation,
        const std::exception& exception)
    {
        nlohmann::json payload = {{"exception_type", typeid(exception).name()}};
        if (const auto* system = dynamic_cast<const std::system_error*>(&exception))
        {
            payload["code"] = system->code().value();
            payload["category"] = system->code().category().name();
        }
        return make_error(operation, "exception", exception.what(), {std::move(payload)});
    }

    template <typename T, typename Callable>
    Result<T> guard(const char* operation, Callable&& callable)
    {
        try
        {
            return std::forward<Callable>(callable)();
        }
        catch (const std::exception& exception)
        {
            return Result<T>::failure(exception_error(operation, exception));
        }
        catch (...)
        {
            return Result<T>::failure(make_error(operation, "unknown_exception", "", {}));
        }
    }
}
