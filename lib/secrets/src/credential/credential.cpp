#include "credential.h"

#include <stdexcept>
#include <system_error>
#include <utility>

#if defined(_WIN32)

#include "platform/windows/credential/windows_credential.h"

namespace secrets
{
    namespace platform_router = windows;
}

#elif defined(__linux__)

#include "platform/linux/credential/linux_credential.h"

namespace secrets
{
    namespace platform_router = linux;
}

#else

#error "Unsupported operating system"

#endif

namespace secrets
{
    SecretResult get(const std::string& signature)
    {
        return platform_router::get_secret(signature);
    }

    std::string resolve(const std::string& signature)
    {
        SecretResult result = get(signature);
        if (result.status == SecretStatus::success)
            return std::move(result.value);

        const std::string operation = result.error.operation.empty()
            ? "credential resolve"
            : result.error.operation;

        if (result.error.code != 0)
        {
            throw std::system_error(
                static_cast<int>(result.error.code),
                std::system_category(),
                operation + ": " + signature);
        }

        throw std::runtime_error(operation + " failed: " + signature);
    }

    SecureString resolve_secure(const std::string& signature)
    {
        SecretResult result = get(signature);
        if (result.status == SecretStatus::success)
            return secure_string_from(result.value);

        const std::string operation = result.error.operation.empty()
            ? "credential resolve"
            : result.error.operation;

        if (result.error.code != 0)
        {
            throw std::system_error(
                static_cast<int>(result.error.code),
                std::system_category(),
                operation + ": " + signature);
        }

        throw std::runtime_error(operation + " failed: " + signature);
    }

    SecureString resolve_secure_session(const std::string& signature)
    {
        SecretResult result = platform_router::get_session_secret(signature);
        if (result.status == SecretStatus::success)
            return secure_string_from(result.value);

        const std::string operation = result.error.operation.empty()
            ? "session credential resolve"
            : result.error.operation;

        if (result.error.code != 0)
        {
            throw std::system_error(
                static_cast<int>(result.error.code),
                std::system_category(),
                operation + ": " + signature);
        }

        throw std::runtime_error(operation + " failed: " + signature);
    }

    SecretOperationResult set(
        const std::string& signature,
        const std::string& value)
    {
        return platform_router::set_secret(signature, value);
    }

    SecretOperationResult set_session(
        const std::string& signature,
        const std::string& value)
    {
        return platform_router::set_session_secret(signature, value);
    }

    SecretOperationResult erase(const std::string& signature)
    {
        return platform_router::erase_secret(signature);
    }

    SecretOperationResult erase_session(const std::string& signature)
    {
        return platform_router::erase_session_secret(signature);
    }
}
