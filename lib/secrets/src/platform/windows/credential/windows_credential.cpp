#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows_credential.h"
#include "credential/validation.h"

#include <Windows.h>
#include <wincred.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

namespace secrets::windows
{
    namespace
    {
        struct credential_deleter
        {
            void operator()(CREDENTIALW* credential) const noexcept
            {
                if (credential == nullptr)
                    return;
                if (credential->CredentialBlob != nullptr)
                    SecureZeroMemory(credential->CredentialBlob, credential->CredentialBlobSize);
                CredFree(credential);
            }
        };
        using unique_credential = std::unique_ptr<CREDENTIALW, credential_deleter>;

        Error system_error(
            const char* operation,
            const char* api,
            DWORD code,
            const std::string& signature)
        {
            return detail::make_error(
                operation, code == ERROR_NOT_FOUND ? "not_found" : "system_error",
                std::system_category().message(static_cast<int>(code)),
                {{{"code", static_cast<std::uint32_t>(code)}, {"category", "system"},
                  {"api", api}, {"signature_bytes", std::vector<unsigned char>(
                      signature.begin(), signature.end())}}});
        }

        Result<std::wstring> build_target_name(const std::string& signature)
        {
            return detail::guard<std::wstring>("build_target_name", [&]
            {
                if (signature.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                {
                    return Result<std::wstring>::failure(detail::make_error(
                        "build_target_name", "validation_error",
                        "Credential signature exceeds the conversion input limit",
                        {{{"field", "signature"}, {"size", signature.size()},
                          {"maximum", (std::numeric_limits<int>::max)()}}}));
                }

                const int required = MultiByteToWideChar(
                    CP_UTF8, MB_ERR_INVALID_CHARS, signature.data(),
                    static_cast<int>(signature.size()), nullptr, 0);
                if (required <= 0)
                {
                    const DWORD code = GetLastError();
                    Error error = system_error("build_target_name", "MultiByteToWideChar", code, signature);
                    error.data.front()["stage"] = "size";
                    return Result<std::wstring>::failure(std::move(error));
                }

                std::wstring target(static_cast<std::size_t>(required), L'\0');
                if (MultiByteToWideChar(
                        CP_UTF8, MB_ERR_INVALID_CHARS, signature.data(),
                        static_cast<int>(signature.size()), target.data(), required) <= 0)
                {
                    const DWORD code = GetLastError();
                    return Result<std::wstring>::failure(
                        system_error("build_target_name", "MultiByteToWideChar", code, signature));
                }
                return Result<std::wstring>::success(std::move(target));
            });
        }

        SecretOperationResult set_with_persist(
            const char* operation,
            const std::string& signature,
            const std::string& value,
            DWORD persist)
        {
            return detail::guard<std::monostate>(operation, [&]
            {
                SecretOperationResult validated = detail::validate_inputs(operation, signature, &value);
                if (validated.error)
                    return validated;
                if (value.size() > static_cast<std::size_t>(CRED_MAX_CREDENTIAL_BLOB_SIZE))
                {
                    return SecretOperationResult::failure(detail::make_error(
                        operation, "validation_error", "Credential value exceeds the backend limit",
                        {{{"field", "value"}, {"size", value.size()},
                          {"maximum", CRED_MAX_CREDENTIAL_BLOB_SIZE}}}));
                }
                Result<std::wstring> target = build_target_name(signature);
                if (target.error)
                    return SecretOperationResult::failure(std::move(*target.error));

                CREDENTIALW credential{};
                credential.Type = CRED_TYPE_GENERIC;
                credential.TargetName = target.value->data();
                credential.CredentialBlobSize = static_cast<DWORD>(value.size());
                credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(value.data()));
                credential.Persist = persist;

                if (!CredWriteW(&credential, 0))
                {
                    const DWORD code = GetLastError();
                    Error error = system_error(operation, "CredWriteW", code, signature);
                    error.data.front()["persist"] = persist;
                    return SecretOperationResult::failure(std::move(error));
                }
                return SecretOperationResult::success(std::monostate{});
            });
        }

        SecretResult get_with_operation(const char* operation, const std::string& signature)
        {
            return detail::guard<std::string>(operation, [&]
            {
                SecretOperationResult validated = detail::validate_inputs(operation, signature);
                if (validated.error)
                    return SecretResult::failure(std::move(*validated.error));
                Result<std::wstring> target = build_target_name(signature);
                if (target.error)
                    return SecretResult::failure(std::move(*target.error));

                PCREDENTIALW raw_credential = nullptr;
                if (!CredReadW(target.value->c_str(), CRED_TYPE_GENERIC, 0, &raw_credential))
                {
                    const DWORD code = GetLastError();
                    return SecretResult::failure(system_error(operation, "CredReadW", code, signature));
                }

                unique_credential credential(raw_credential);
                if (credential == nullptr ||
                    (credential->CredentialBlobSize != 0 && credential->CredentialBlob == nullptr))
                {
                    return SecretResult::failure(detail::make_error(
                        operation, "protocol_error", "CredReadW returned an invalid credential blob",
                        {{{"api", "CredReadW"}}}));
                }
                std::string value;
                if (credential->CredentialBlobSize != 0)
                {
                    value.assign(reinterpret_cast<const char*>(credential->CredentialBlob),
                        static_cast<std::size_t>(credential->CredentialBlobSize));
                }
                return SecretResult::success(std::move(value));
            });
        }

        SecretOperationResult erase_with_operation(const char* operation, const std::string& signature)
        {
            return detail::guard<std::monostate>(operation, [&]
            {
                SecretOperationResult validated = detail::validate_inputs(operation, signature);
                if (validated.error)
                    return validated;
                Result<std::wstring> target = build_target_name(signature);
                if (target.error)
                    return SecretOperationResult::failure(std::move(*target.error));

                if (!CredDeleteW(target.value->c_str(), CRED_TYPE_GENERIC, 0))
                {
                    const DWORD code = GetLastError();
                    return SecretOperationResult::failure(system_error(operation, "CredDeleteW", code, signature));
                }
                return SecretOperationResult::success(std::monostate{});
            });
        }
    }

    SecretResult get_secret(const std::string& signature)
    {
        return get_with_operation("get", signature);
    }

    SecretResult get_session_secret(const std::string& signature)
    {
        return get_with_operation("get_session", signature);
    }

    SecretOperationResult set_secret(const std::string& signature, const std::string& value)
    {
        return set_with_persist("set", signature, value, CRED_PERSIST_LOCAL_MACHINE);
    }

    SecretOperationResult set_session_secret(const std::string& signature, const std::string& value)
    {
        return set_with_persist("set_session", signature, value, CRED_PERSIST_SESSION);
    }

    SecretOperationResult erase_secret(const std::string& signature)
    {
        return erase_with_operation("erase", signature);
    }

    SecretOperationResult erase_session_secret(const std::string& signature)
    {
        return erase_with_operation("erase_session", signature);
    }
}
