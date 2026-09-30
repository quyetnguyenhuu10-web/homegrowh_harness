#include "error.h"

#include <typeinfo>
#include <utility>

namespace ipc
{
    void to_json(nlohmann::json& json, const Error& error)
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
}

namespace ipc::detail
{
    Error make_error(
        std::string_view operation,
        std::string_view type,
        std::string_view message,
        nlohmann::json&& context)
    {
        Error error{
            "ipc", std::string(operation), std::string(type),
            std::string(message), {}, {},
        };
        if (!context.is_null())
            error.data.push_back(std::move(context));
        return error;
    }

    Error make_system_error(
        std::string_view operation,
        const std::error_code& code,
        std::string_view api,
        nlohmann::json&& context)
    {
        context["code"] = code.value();
        context["category"] = code.category().name();
        context["api"] = api;
        return make_error(operation, "system_error", code.message(),
            std::move(context));
    }

    Error make_exception_error(
        std::string_view operation,
        const std::exception& exception,
        nlohmann::json&& context)
    {
        context["exception_type"] = typeid(exception).name();
        if (const auto* system = dynamic_cast<const std::system_error*>(&exception))
        {
            context["code"] = system->code().value();
            context["category"] = system->code().category().name();
            return make_error(operation, "system_error", exception.what(),
                std::move(context));
        }
        if (const auto* json = dynamic_cast<const nlohmann::json::exception*>(&exception))
            context["code"] = json->id;
        return make_error(operation, "exception", exception.what(),
            std::move(context));
    }

    void add_cleanup_error(std::optional<Error>& error, Error&& cleanup_error)
    {
        if (!error)
        {
            error = std::move(cleanup_error);
            return;
        }
        Error combined = make_error(error->operation, "dependency_error",
            "IPC operation and resource cleanup failed");
        combined.causes.push_back(std::move(*error));
        combined.causes.push_back(std::move(cleanup_error));
        error = std::move(combined);
    }

    std::optional<Error> validate_name(
        const std::string& name, std::string_view operation)
    {
        if (name.empty() || name.find('\0') != std::string::npos
            || name.find('/') != std::string::npos
            || name.find('\\') != std::string::npos)
        {
            return make_error(operation, "validation_error",
                "Endpoint name must be nonempty and contain no NUL or path separators",
                {{"name", name}});
        }
        return std::nullopt;
    }
}
