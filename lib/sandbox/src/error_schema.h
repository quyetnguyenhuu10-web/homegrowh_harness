#pragma once

#include <sandbox>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace sandbox::detail
{
    inline std::string error_api_name(std::string_view operation)
    {
        return std::string(operation.substr(0, operation.find('(')));
    }

    inline std::string error_path_text(const std::filesystem::path& path)
    {
        const std::u8string value = path.u8string();
        return std::string(
            reinterpret_cast<const char*>(value.data()),
            value.size());
    }

    inline Error make_error(
        std::string operation,
        std::string type,
        std::string message,
        nlohmann::json data = nullptr,
        std::vector<Error> causes = {})
    {
        nlohmann::json::array_t data_items;
        if (!data.is_null())
        {
            if (data.is_array())
                data_items = std::move(data).get<nlohmann::json::array_t>();
            else
                data_items.push_back(std::move(data));
        }

        return Error{
            "sandbox",
            std::move(operation),
            std::move(type),
            std::move(message),
            std::move(data_items),
            std::move(causes),
        };
    }

    inline Error make_system_error(
        std::string_view operation,
        const std::error_code& code,
        const std::filesystem::path* path = nullptr)
    {
        nlohmann::json data = {
            {"code", code.value()},
            {"category", code.category().name()},
            {"api", error_api_name(operation)},
        };
        if (path != nullptr)
            data["path"] = error_path_text(*path);

        return make_error(
            std::string(operation),
            "system_error",
            code.message(),
            std::move(data));
    }

    inline Error make_system_error(
        std::string_view operation,
        const std::error_code& code,
        const std::filesystem::path& path)
    {
        return make_system_error(operation, code, &path);
    }

    inline Error make_system_error(
        std::string_view operation,
        const std::error_code& code,
        const std::optional<std::filesystem::path>& path)
    {
        return make_system_error(operation, code, path ? &*path : nullptr);
    }

    inline Error make_native_error(
        std::string_view operation,
        std::uint32_t code,
        const std::filesystem::path* path = nullptr)
    {
        Error error = make_system_error(
            operation,
            std::error_code(static_cast<int>(code), std::system_category()),
            path);
        error.data.front()["code"] = code;
        return error;
    }

    inline Error make_native_error(
        std::string_view operation,
        std::uint32_t code,
        const std::filesystem::path& path)
    {
        return make_native_error(operation, code, &path);
    }

    inline Error make_native_error(
        std::string_view operation,
        std::uint32_t code,
        const std::optional<std::filesystem::path>& path)
    {
        return make_native_error(operation, code, path ? &*path : nullptr);
    }

    class error_exception final : public std::exception
    {
    public:
        explicit error_exception(Error&& error)
            : error_(std::move(error))
        {
        }

        const char* what() const noexcept override
        {
            return error_.message.c_str();
        }

        const Error& error() const noexcept
        {
            return error_;
        }

        Error take_error() && noexcept
        {
            return std::move(error_);
        }

    private:
        Error error_;
    };

    [[noreturn]] inline void throw_error(Error&& error)
    {
        throw error_exception(std::move(error));
    }

    [[nodiscard]] Error make_exception_error(
        std::string operation,
        const std::exception& exception,
        nlohmann::json data = nullptr);

    [[nodiscard]] Error capture_exception(
        std::string operation,
        std::exception_ptr exception,
        nlohmann::json data = nullptr);
}
