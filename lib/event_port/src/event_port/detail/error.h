#pragma once

#include <event_port>

#include <exception>
#include <new>
#include <system_error>
#include <typeinfo>
#include <utility>

namespace event_port::detail
{
    inline Error exception_error(
        const char* operation,
        const std::exception& exception)
    {
        nlohmann::json payload = {{"exception_type", typeid(exception).name()}};
        const char* type = "exception";
        if (const auto* system = dynamic_cast<const std::system_error*>(&exception))
        {
            type = "system_error";
            payload["code"] = system->code().value();
            payload["category"] = system->code().category().name();
        }
        else if (dynamic_cast<const std::bad_alloc*>(&exception) != nullptr)
        {
            type = "allocation_error";
        }
        else if (const auto* json = dynamic_cast<const nlohmann::json::exception*>(&exception))
        {
            type = "json_error";
            payload["exception_id"] = json->id;
        }
        return {"event_port", operation, type, exception.what(), {std::move(payload)}, {}};
    }

    template <typename T, typename Function>
    Result<T> guard(const char* operation, Function&& function)
    {
        try
        {
            return std::forward<Function>(function)();
        }
        catch (const std::exception& exception)
        {
            return Result<T>::failure(exception_error(operation, exception));
        }
        catch (...)
        {
            return Result<T>::failure(
                Error{"event_port", operation, "unknown_exception", "", {}, {}});
        }
    }

    inline Error context_error(
        const char* operation,
        const char* type,
        const char* message,
        nlohmann::json::array_t&& data,
        Error&& cause)
    {
        std::vector<Error> causes;
        causes.emplace_back(std::move(cause));
        return {"event_port", operation, type, message, std::move(data), std::move(causes)};
    }
}
