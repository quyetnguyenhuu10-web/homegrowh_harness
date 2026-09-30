#include "linux_credential.h"
#include "keyutils_api.h"
#include "credential/validation.h"
#include "memory/wipe.h"

#include <cerrno>
#include <limits>
#include <system_error>
#include <utility>

namespace secrets::linux
{
    namespace
    {
        using detail::KeyutilsApi;
        using detail::key_serial_t;
        enum class StorageScope { persistent, session };

        Error system_error(
            const char* operation,
            const char* api,
            int code,
            long result,
            nlohmann::json&& context = {})
        {
            context["code"] = code;
            context["errno"] = code;
            context["category"] = "generic";
            context["api"] = api;
            context["result"] = result;
            return secrets::detail::make_error(
                operation, code == ENOKEY ? "not_found" : "system_error",
                code == 0 ? "" : std::generic_category().message(code), {std::move(context)});
        }

        Result<key_serial_t> key_id(const char* api, long value)
        {
            if (value > (std::numeric_limits<key_serial_t>::max)())
            {
                return Result<key_serial_t>::failure(secrets::detail::make_error(
                    api, "protocol_error", "Key identifier exceeds the backend result range",
                    {{{"api", api}, {"result", value},
                      {"maximum", (std::numeric_limits<key_serial_t>::max)()}}}));
            }
            return Result<key_serial_t>::success(static_cast<key_serial_t>(value));
        }

        Result<key_serial_t> storage_keyring(const KeyutilsApi& keyutils, StorageScope scope)
        {
            if (scope == StorageScope::session)
                return Result<key_serial_t>::success(key_serial_t{detail::session_keyring});
            if (keyutils.get_persistent == nullptr)
                return Result<key_serial_t>::success(key_serial_t{detail::user_keyring});

            errno = 0;
            const long result = keyutils.get_persistent(static_cast<uid_t>(-1), detail::user_keyring);
            if (result < 0)
            {
                const int code = errno;
                if (code == EOPNOTSUPP || code == ENOSYS)
                    return Result<key_serial_t>::success(key_serial_t{detail::user_keyring});
                return Result<key_serial_t>::failure(system_error(
                    "storage_keyring", "keyctl_get_persistent", code, result,
                    {{"keyring", detail::user_keyring}}));
            }
            return key_id("keyctl_get_persistent", result);
        }

        Result<key_serial_t> search_key(
            const KeyutilsApi& keyutils, key_serial_t keyring, const std::string& signature)
        {
            errno = 0;
            const long result = keyutils.search(keyring, detail::key_type, signature.c_str(), 0);
            if (result < 0)
            {
                const int code = errno;
                return Result<key_serial_t>::failure(system_error(
                    "search_key", "keyctl_search", code, result,
                    {{"keyring", keyring}, {"signature_bytes", std::vector<unsigned char>(
                        signature.begin(), signature.end())}}));
            }
            return key_id("keyctl_search", result);
        }

        SecretResult read_key(const KeyutilsApi& keyutils, key_serial_t key)
        {
            return secrets::detail::guard<std::string>("read_key", [&]
            {
                errno = 0;
                const long required = keyutils.read(key, nullptr, 0);
                if (required < 0)
                {
                    const int code = errno;
                    Error error = system_error("read_key", "keyctl_read", code, required, {{"key", key}});
                    error.data.front()["stage"] = "size";
                    return SecretResult::failure(std::move(error));
                }

                std::string value(static_cast<std::size_t>(required), '\0');
                secrets::detail::StringWiper wipe(value);
                for (;;)
                {
                    errno = 0;
                    const long bytes = keyutils.read(
                        key, value.empty() ? nullptr : value.data(), value.size());
                    if (bytes < 0)
                    {
                        const int code = errno;
                        return SecretResult::failure(
                            system_error("read_key", "keyctl_read", code, bytes, {{"key", key}}));
                    }
                    const auto size = static_cast<std::size_t>(bytes);
                    if (size <= value.size())
                    {
                        value.resize(size);
                        return SecretResult::success(std::move(value));
                    }
                    secrets::detail::secure_zero(value.data(), value.size());
                    value.resize(size);
                }
            });
        }

        SecretResult get_in_scope(
            const char* operation, const std::string& signature, StorageScope scope)
        {
            return secrets::detail::guard<std::string>(operation, [&]
            {
                auto validated = secrets::detail::validate_inputs(operation, signature);
                if (validated.error)
                    return SecretResult::failure(std::move(*validated.error));
                auto api = detail::keyutils_api();
                if (api.error)
                    return SecretResult::failure(std::move(*api.error));
                auto keyring = storage_keyring(**api.value, scope);
                if (keyring.error)
                    return SecretResult::failure(std::move(*keyring.error));
                auto key = search_key(**api.value, *keyring.value, signature);
                if (key.error)
                    return SecretResult::failure(std::move(*key.error));
                return read_key(**api.value, *key.value);
            });
        }

        SecretOperationResult set_in_scope(
            const char* operation,
            const std::string& signature,
            const std::string& value,
            StorageScope scope)
        {
            return secrets::detail::guard<std::monostate>(operation, [&]
            {
                auto validated = secrets::detail::validate_inputs(operation, signature, &value);
                if (validated.error)
                    return validated;
                auto api = detail::keyutils_api();
                if (api.error)
                    return SecretOperationResult::failure(std::move(*api.error));
                const auto& keyutils = **api.value;
                auto keyring = storage_keyring(keyutils, scope);
                if (keyring.error)
                    return SecretOperationResult::failure(std::move(*keyring.error));
                auto key = search_key(keyutils, *keyring.value, signature);
                if (key.value)
                {
                    errno = 0;
                    const long result = keyutils.update(*key.value, value.data(), value.size());
                    if (result < 0)
                    {
                        const int code = errno;
                        return SecretOperationResult::failure(system_error(
                            operation, "keyctl_update", code, result, {{"key", *key.value}}));
                    }
                    return SecretOperationResult::success(std::monostate{});
                }
                if (key.error->type != "not_found")
                    return SecretOperationResult::failure(std::move(*key.error));

                errno = 0;
                const key_serial_t result = keyutils.add_key(
                    detail::key_type, signature.c_str(), value.data(), value.size(), *keyring.value);
                if (result < 0)
                {
                    const int code = errno;
                    return SecretOperationResult::failure(system_error(
                        operation, "add_key", code, result,
                        {{"keyring", *keyring.value}, {"signature_bytes", std::vector<unsigned char>(
                            signature.begin(), signature.end())}}));
                }
                return SecretOperationResult::success(std::monostate{});
            });
        }

        SecretOperationResult erase_in_scope(
            const char* operation, const std::string& signature, StorageScope scope)
        {
            return secrets::detail::guard<std::monostate>(operation, [&]
            {
                auto validated = secrets::detail::validate_inputs(operation, signature);
                if (validated.error)
                    return validated;
                auto api = detail::keyutils_api();
                if (api.error)
                    return SecretOperationResult::failure(std::move(*api.error));
                const auto& keyutils = **api.value;
                auto keyring = storage_keyring(keyutils, scope);
                if (keyring.error)
                    return SecretOperationResult::failure(std::move(*keyring.error));
                auto key = search_key(keyutils, *keyring.value, signature);
                if (key.error)
                    return SecretOperationResult::failure(std::move(*key.error));
                errno = 0;
                const long result = keyutils.unlink(*key.value, *keyring.value);
                if (result < 0)
                {
                    const int code = errno;
                    return SecretOperationResult::failure(system_error(
                        operation, "keyctl_unlink", code, result,
                        {{"key", *key.value}, {"keyring", *keyring.value}}));
                }
                return SecretOperationResult::success(std::monostate{});
            });
        }
    }

    SecretResult get_secret(const std::string& signature)
    {
        return get_in_scope("get", signature, StorageScope::persistent);
    }

    SecretResult get_session_secret(const std::string& signature)
    {
        return get_in_scope("get_session", signature, StorageScope::session);
    }

    SecretOperationResult set_secret(const std::string& signature, const std::string& value)
    {
        return set_in_scope("set", signature, value, StorageScope::persistent);
    }

    SecretOperationResult set_session_secret(const std::string& signature, const std::string& value)
    {
        return set_in_scope("set_session", signature, value, StorageScope::session);
    }

    SecretOperationResult erase_secret(const std::string& signature)
    {
        return erase_in_scope("erase", signature, StorageScope::persistent);
    }

    SecretOperationResult erase_session_secret(const std::string& signature)
    {
        return erase_in_scope("erase_session", signature, StorageScope::session);
    }
}
