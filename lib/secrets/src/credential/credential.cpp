#include "credential.h"
#include "error/error.h"

#include <utility>

#if defined(_WIN32)
#include "platform/windows/credential/windows_credential.h"
namespace secrets { namespace platform_router = windows; }
#elif defined(__linux__)
#include "platform/linux/credential/linux_credential.h"
namespace secrets { namespace platform_router = linux; }
#else
#error "Unsupported operating system"
#endif

namespace secrets
{
    SecretResult get(const std::string& signature)
    {
        return detail::guard<std::string>("get", [&]
        {
            return platform_router::get_secret(signature);
        });
    }

    SecretResult resolve(const std::string& signature)
    {
        return get(signature);
    }

    SecureSecretResult resolve_secure(const std::string& signature)
    {
        SecretResult result = get(signature);
        if (result.error)
            return SecureSecretResult::failure(std::move(*result.error));
        return secure_string_from(*result.value);
    }

    SecureSecretResult resolve_secure_session(const std::string& signature)
    {
        return detail::guard<SecureString>("resolve_secure_session", [&]
        {
            SecretResult result = platform_router::get_session_secret(signature);
            if (result.error)
                return SecureSecretResult::failure(std::move(*result.error));
            return secure_string_from(*result.value);
        });
    }

    SecretOperationResult set(const std::string& signature, const std::string& value)
    {
        return detail::guard<std::monostate>("set", [&]
        {
            return platform_router::set_secret(signature, value);
        });
    }

    SecretOperationResult set_session(const std::string& signature, const std::string& value)
    {
        return detail::guard<std::monostate>("set_session", [&]
        {
            return platform_router::set_session_secret(signature, value);
        });
    }

    SecretOperationResult erase(const std::string& signature)
    {
        return detail::guard<std::monostate>("erase", [&]
        {
            return platform_router::erase_secret(signature);
        });
    }

    SecretOperationResult erase_session(const std::string& signature)
    {
        return detail::guard<std::monostate>("erase_session", [&]
        {
            return platform_router::erase_session_secret(signature);
        });
    }
}
