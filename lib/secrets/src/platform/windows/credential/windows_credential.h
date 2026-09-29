#pragma once

#include "credential/credential.h"

#include <string>

namespace secrets::windows
{
    SecretResult get_secret(const std::string& signature);
    SecretResult get_session_secret(const std::string& signature);

    SecretOperationResult set_secret(
        const std::string& signature,
        const std::string& value);
    SecretOperationResult set_session_secret(
        const std::string& signature,
        const std::string& value);

    SecretOperationResult erase_secret(const std::string& signature);
    SecretOperationResult erase_session_secret(const std::string& signature);
}
