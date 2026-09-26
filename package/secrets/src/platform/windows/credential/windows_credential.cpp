#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_credential.h"

#include <Windows.h>
#include <wincred.h>

#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace secrets::windows
{
    namespace
    {
        struct credential_deleter
        {
            void operator()(CREDENTIALW* credential) const noexcept
            {
                if (credential != nullptr)
                    CredFree(credential);
            }
        };

        using unique_credential =
            std::unique_ptr<CREDENTIALW, credential_deleter>;

        SecretError os_error(const char* operation, DWORD error);
        SecretError validation_error(const char* operation, DWORD error);
        bool build_target_name(
            const std::string& signature,
            std::wstring& target,
            SecretError& error);

        SecretOperationResult set_secret_with_persist(
            const std::string& signature,
            const std::string& value,
            DWORD persist)
        {
            std::wstring target;
            SecretError error;
            if (!build_target_name(signature, target, error))
            {
                return {
                    SecretStatus::failed,
                    std::move(error),
                };
            }

            if (value.empty())
            {
                return {
                    SecretStatus::failed,
                    validation_error(
                        "validate credential value",
                        ERROR_INVALID_PARAMETER),
                };
            }

            if (value.size() > static_cast<std::size_t>(CRED_MAX_CREDENTIAL_BLOB_SIZE))
            {
                return {
                    SecretStatus::failed,
                    validation_error(
                        "validate credential value",
                        ERROR_BUFFER_OVERFLOW),
                };
            }

            CREDENTIALW credential{};
            credential.Type = CRED_TYPE_GENERIC;
            credential.TargetName = target.data();
            credential.CredentialBlobSize = static_cast<DWORD>(value.size());
            credential.CredentialBlob = reinterpret_cast<LPBYTE>(
                const_cast<char*>(value.data()));
            credential.Persist = persist;

            if (!CredWriteW(&credential, 0))
            {
                const DWORD error_code = GetLastError();
                return {
                    SecretStatus::failed,
                    os_error("CredWriteW", error_code),
                };
            }

            return {
                SecretStatus::success,
                {},
            };
        }

        SecretError os_error(
            const char* operation,
            const DWORD error)
        {
            return {
                static_cast<std::uint32_t>(error),
                operation,
            };
        }

        SecretError validation_error(
            const char* operation,
            const DWORD error)
        {
            return {
                static_cast<std::uint32_t>(error),
                operation,
            };
        }

        bool build_target_name(
            const std::string& signature,
            std::wstring& target,
            SecretError& error)
        {
            if (signature.empty())
            {
                error = validation_error(
                    "validate credential signature",
                    ERROR_INVALID_PARAMETER);
                return false;
            }

            if (signature.size()
                > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
            {
                error = validation_error(
                    "validate credential signature",
                    ERROR_BUFFER_OVERFLOW);
                return false;
            }

            const int required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                signature.data(),
                static_cast<int>(signature.size()),
                nullptr,
                0);
            if (required <= 0)
            {
                const DWORD error_code = GetLastError();
                error = os_error(
                    "MultiByteToWideChar(size)",
                    error_code);
                return false;
            }

            std::wstring converted(
                static_cast<std::size_t>(required),
                L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    signature.data(),
                    static_cast<int>(signature.size()),
                    converted.data(),
                    required) <= 0)
            {
                const DWORD error_code = GetLastError();
                error = os_error(
                    "MultiByteToWideChar",
                    error_code);
                return false;
            }

            target = std::move(converted);
            return true;
        }
    }

    SecretResult get_secret(const std::string& signature)
    {
        std::wstring target;
        SecretError error;
        if (!build_target_name(signature, target, error))
        {
            return {
                SecretStatus::failed,
                {},
                std::move(error),
            };
        }

        PCREDENTIALW raw_credential = nullptr;
        if (!CredReadW(
                target.c_str(),
                CRED_TYPE_GENERIC,
                0,
                &raw_credential))
        {
            const DWORD error_code = GetLastError();
            return {
                error_code == ERROR_NOT_FOUND
                    ? SecretStatus::not_found
                    : SecretStatus::failed,
                {},
                os_error("CredReadW", error_code),
            };
        }

        unique_credential credential(raw_credential);
        std::string value;
        if (credential->CredentialBlobSize != 0)
        {
            const auto* bytes = reinterpret_cast<const char*>(
                credential->CredentialBlob);
            value.assign(
                bytes,
                static_cast<std::size_t>(credential->CredentialBlobSize));
        }

        return {
            SecretStatus::success,
            std::move(value),
            {},
        };
    }

    SecretResult get_session_secret(const std::string& signature)
    {
        return get_secret(signature);
    }

    SecretOperationResult set_secret(
        const std::string& signature,
        const std::string& value)
    {
        return set_secret_with_persist(
            signature,
            value,
            CRED_PERSIST_LOCAL_MACHINE);
    }

    SecretOperationResult set_session_secret(
        const std::string& signature,
        const std::string& value)
    {
        return set_secret_with_persist(
            signature,
            value,
            CRED_PERSIST_SESSION);
    }

    SecretOperationResult erase_secret(const std::string& signature)
    {
        std::wstring target;
        SecretError error;
        if (!build_target_name(signature, target, error))
        {
            return {
                SecretStatus::failed,
                std::move(error),
            };
        }

        if (!CredDeleteW(
                target.c_str(),
                CRED_TYPE_GENERIC,
                0))
        {
            const DWORD error_code = GetLastError();
            return {
                error_code == ERROR_NOT_FOUND
                    ? SecretStatus::not_found
                    : SecretStatus::failed,
                os_error("CredDeleteW", error_code),
            };
        }

        return {
            SecretStatus::success,
            {},
        };
    }

    SecretOperationResult erase_session_secret(const std::string& signature)
    {
        return erase_secret(signature);
    }
}
