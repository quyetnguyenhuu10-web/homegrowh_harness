#include "../error_schema.h"

#include <new>
#include <typeinfo>

namespace sandbox::detail
{
    namespace
    {
        void append_data(Error& error, nlohmann::json&& data)
        {
            if (data.is_null())
                return;
            if (data.is_array())
            {
                for (auto& item : data)
                    error.data.push_back(std::move(item));
            }
            else
                error.data.push_back(std::move(data));
        }
    }

    Error make_exception_error(
        std::string operation,
        const std::exception& exception,
        nlohmann::json data)
    {
        if (const auto* structured = dynamic_cast<const error_exception*>(&exception))
            return structured->error();

        nlohmann::json metadata = {
            {"exception_type", typeid(exception).name()},
        };
        std::string type = "exception";
        if (const auto* filesystem =
                dynamic_cast<const std::filesystem::filesystem_error*>(&exception))
        {
            type = "system_error";
            metadata["code"] = filesystem->code().value();
            metadata["category"] = filesystem->code().category().name();
            metadata["path1"] = error_path_text(filesystem->path1());
            metadata["path2"] = error_path_text(filesystem->path2());
        }
        else if (const auto* system = dynamic_cast<const std::system_error*>(&exception))
        {
            type = "system_error";
            metadata["code"] = system->code().value();
            metadata["category"] = system->code().category().name();
        }
        else if (const auto* json = dynamic_cast<const nlohmann::json::exception*>(&exception))
        {
            type = "protocol_error";
            metadata["id"] = json->id;
            if (const auto* parse =
                    dynamic_cast<const nlohmann::json::parse_error*>(json))
                metadata["byte"] = parse->byte;
        }
        else if (dynamic_cast<const std::bad_alloc*>(&exception) != nullptr)
            type = "resource_error";

        Error error = make_error(
            std::move(operation), std::move(type), exception.what(), std::move(metadata));
        append_data(error, std::move(data));
        return error;
    }

    Error capture_exception(
        std::string operation,
        std::exception_ptr exception,
        nlohmann::json data)
    {
        if (!exception)
        {
            return make_error(
                std::move(operation), "invalid_argument",
                "No exception was supplied", std::move(data));
        }
        try
        {
            std::rethrow_exception(exception);
        }
        catch (const error_exception& structured)
        {
            return structured.error();
        }
        catch (const Error& structured)
        {
            return structured;
        }
        catch (const std::exception& native)
        {
            return make_exception_error(std::move(operation), native, std::move(data));
        }
        catch (const nlohmann::json& payload)
        {
            Error error = make_error(
                std::move(operation), "exception", "A JSON value was thrown", payload);
            append_data(error, std::move(data));
            return error;
        }
        catch (const std::string& message)
        {
            return make_error(
                std::move(operation), "exception", message, std::move(data));
        }
        catch (const char* message)
        {
            return make_error(
                std::move(operation), "exception",
                message == nullptr ? "A null string was thrown" : message,
                std::move(data));
        }
        catch (...)
        {
            return make_error(
                std::move(operation), "exception",
                "A non-standard exception was thrown", std::move(data));
        }
    }
}
