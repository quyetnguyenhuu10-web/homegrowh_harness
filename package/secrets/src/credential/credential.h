#pragma once

#include <cstdint>
#include <string>

#include <memory/secure_string.h>

namespace secrets
{
    enum class SecretStatus
    {
        success,
        not_found,
        failed,
    };

    struct SecretError
    {
        std::uint32_t code = 0;
        std::string operation;
    };

    struct SecretResult
    {
        SecretStatus status = SecretStatus::failed;
        std::string value;
        SecretError error;
    };

    struct SecretOperationResult
    {
        SecretStatus status = SecretStatus::failed;
        SecretError error;
    };

    SecretResult get(const std::string& signature);

    std::string resolve(const std::string& signature);

    SecureString resolve_secure(const std::string& signature);

    SecureString resolve_secure_session(const std::string& signature);

    SecretOperationResult set(
        const std::string& signature,
        const std::string& value);

    SecretOperationResult set_session(
        const std::string& signature,
        const std::string& value);

    SecretOperationResult erase(const std::string& signature);

    SecretOperationResult erase_session(const std::string& signature);
}
