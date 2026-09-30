#pragma once

#include "error/error.h"

namespace secrets::detail
{
    inline SecretOperationResult validate_inputs(
        const char* operation,
        const std::string& signature,
        const std::string* value = nullptr)
    {
        std::vector<Error> errors;
        if (signature.empty())
        {
            errors.push_back(make_error(
                operation, "validation_error", "Credential signature must not be empty",
                {{{"field", "signature"}, {"size", signature.size()}}}));
        }
        else if (const auto offset = signature.find('\0'); offset != std::string::npos)
        {
            errors.push_back(make_error(
                operation, "validation_error", "Credential signature contains a null byte",
                {{{"field", "signature"}, {"offset", offset}, {"size", signature.size()}}}));
        }
        if (value != nullptr && value->empty())
        {
            errors.push_back(make_error(
                operation, "validation_error", "Credential value must not be empty",
                {{{"field", "value"}, {"size", value->size()}}}));
        }
        if (errors.empty())
            return SecretOperationResult::success(std::monostate{});
        if (errors.size() == 1)
            return SecretOperationResult::failure(std::move(errors.front()));
        return SecretOperationResult::failure(make_error(
            operation, "validation_error", "Credential input is invalid", {}, std::move(errors)));
    }
}
