#include "capture.h"

#include <new>
#include <system_error>
#include <typeinfo>

namespace provider::error_detail
{
    Error make_error(
        std::string_view operation,
        std::string_view type,
        std::string_view message,
        nlohmann::json::array_t&& data)
    {
        return Error{
            "provider", std::string(operation), std::string(type),
            std::string(message), std::move(data), {}};
    }

    Error capture_exception(
        const std::exception_ptr& exception,
        std::string_view operation,
        nlohmann::json::array_t&& data)
    {
        try
        {
            if (exception != nullptr)
            {
                std::rethrow_exception(exception);
            }
        }
        catch (Error& error)
        {
            // An already normalized error keeps its identity and payload.
            return std::move(error);
        }
        catch (const nlohmann::json::exception& error)
        {
            nlohmann::json details = {
                {"category", "nlohmann_json"},
                {"code", error.id},
                {"exception_type", typeid(error).name()}};
            if (const auto* parse =
                    dynamic_cast<const nlohmann::json::parse_error*>(&error))
            {
                details["byte"] = parse->byte;
            }
            data.push_back(std::move(details));
            return make_error(
                operation, "protocol_error", error.what(), std::move(data));
        }
        catch (const std::system_error& error)
        {
            data.push_back({
                {"code", error.code().value()},
                {"category", error.code().category().name()},
                {"exception_type", typeid(error).name()}});
            return make_error(
                operation, "system_error", error.what(), std::move(data));
        }
        catch (const std::bad_alloc& error)
        {
            data.push_back({{"exception_type", typeid(error).name()}});
            return make_error(
                operation, "resource_error", error.what(), std::move(data));
        }
        catch (const std::exception& error)
        {
            data.push_back({{"exception_type", typeid(error).name()}});
            return make_error(
                operation, "exception_error", error.what(), std::move(data));
        }
        catch (...)
        {
            data.push_back({{"exception_type", "unknown"}});
            return make_error(
                operation, "exception_error", "Non-standard exception",
                std::move(data));
        }

        return make_error(
            operation, "invalid_state", "No captured exception",
            std::move(data));
    }

    Error dependency_error(
        std::string_view operation,
        std::string_view message,
        Error&& cause,
        nlohmann::json::array_t&& data)
    {
        Error error = make_error(
            operation, "dependency_error", message, std::move(data));
        error.causes.push_back(std::move(cause));
        return error;
    }
}
