#include "linux_credential.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <memory>
#include <string>
#include <sys/types.h>
#include <utility>

namespace secrets::linux
{
    namespace
    {
        using key_serial_t = std::int32_t;

        constexpr key_serial_t key_spec_session_keyring = -3;
        constexpr key_serial_t key_spec_user_keyring = -4;
        constexpr const char* key_type = "user";

        enum class storage_scope
        {
            persistent,
            session,
        };

        struct library_deleter
        {
            void operator()(void* library) const noexcept
            {
                if (library != nullptr)
                    dlclose(library);
            }
        };

        using unique_library = std::unique_ptr<void, library_deleter>;

        using add_key_fn = key_serial_t (*)(
            const char*,
            const char*,
            const void*,
            std::size_t,
            key_serial_t);
        using keyctl_get_persistent_fn = long (*)(uid_t, key_serial_t);
        using keyctl_search_fn = long (*)(
            key_serial_t,
            const char*,
            const char*,
            key_serial_t);
        using keyctl_read_fn = long (*)(key_serial_t, char*, std::size_t);
        using keyctl_update_fn = long (*)(
            key_serial_t,
            const void*,
            std::size_t);
        using keyctl_unlink_fn = long (*)(key_serial_t, key_serial_t);

        struct keyutils_api
        {
            unique_library library;
            add_key_fn add_key = nullptr;
            keyctl_get_persistent_fn get_persistent = nullptr;
            keyctl_search_fn search = nullptr;
            keyctl_read_fn read = nullptr;
            keyctl_update_fn update = nullptr;
            keyctl_unlink_fn unlink = nullptr;
        };

        SecretError error(const char* operation, int code)
        {
            return {
                static_cast<std::uint32_t>(code),
                operation,
            };
        }

        template <typename Function>
        Function symbol(void* library, const char* name) noexcept
        {
            return reinterpret_cast<Function>(dlsym(library, name));
        }

        bool load_keyutils(keyutils_api& api, SecretError& load_error)
        {
            void* raw_library = dlopen("libkeyutils.so.1", RTLD_NOW | RTLD_LOCAL);
            if (raw_library == nullptr)
                raw_library = dlopen("libkeyutils.so", RTLD_NOW | RTLD_LOCAL);

            if (raw_library == nullptr)
            {
                load_error = error("dlopen(libkeyutils)", ENOSYS);
                return false;
            }

            unique_library library(raw_library);
            keyutils_api loaded;
            loaded.add_key = symbol<add_key_fn>(raw_library, "add_key");
            loaded.get_persistent = symbol<keyctl_get_persistent_fn>(
                raw_library,
                "keyctl_get_persistent");
            loaded.search = symbol<keyctl_search_fn>(raw_library, "keyctl_search");
            loaded.read = symbol<keyctl_read_fn>(raw_library, "keyctl_read");
            loaded.update = symbol<keyctl_update_fn>(raw_library, "keyctl_update");
            loaded.unlink = symbol<keyctl_unlink_fn>(raw_library, "keyctl_unlink");

            if (
                loaded.add_key == nullptr ||
                loaded.search == nullptr ||
                loaded.read == nullptr ||
                loaded.update == nullptr ||
                loaded.unlink == nullptr)
            {
                load_error = error("dlsym(libkeyutils)", ENOSYS);
                return false;
            }

            loaded.library = std::move(library);
            api = std::move(loaded);
            return true;
        }

        const keyutils_api* api(SecretError& load_error)
        {
            struct state
            {
                keyutils_api value;
                SecretError error;
                bool loaded = false;

                state()
                {
                    loaded = load_keyutils(value, error);
                }
            };

            static const state shared;
            if (!shared.loaded)
            {
                load_error = shared.error;
                return nullptr;
            }
            return &shared.value;
        }

        bool validate_signature(
            const std::string& signature,
            SecretError& validation_error)
        {
            if (!signature.empty())
                return true;

            validation_error = error("validate credential signature", EINVAL);
            return false;
        }

        bool validate_value(
            const std::string& value,
            SecretError& validation_error)
        {
            if (!value.empty())
                return true;

            validation_error = error("validate credential value", EINVAL);
            return false;
        }

        bool storage_keyring(
            const keyutils_api& keyutils,
            storage_scope scope,
            key_serial_t& keyring,
            SecretError& operation_error)
        {
            if (scope == storage_scope::session)
            {
                keyring = key_spec_session_keyring;
                return true;
            }

            if (keyutils.get_persistent == nullptr)
            {
                keyring = key_spec_user_keyring;
                return true;
            }

            errno = 0;
            const long result = keyutils.get_persistent(
                static_cast<uid_t>(-1),
                key_spec_user_keyring);
            if (result < 0)
            {
                const int code = errno == 0 ? EIO : errno;
                if (code == EOPNOTSUPP || code == ENOSYS)
                {
                    keyring = key_spec_user_keyring;
                    return true;
                }

                operation_error = error(
                    "keyctl_get_persistent",
                    code);
                return false;
            }

            if (result > (std::numeric_limits<key_serial_t>::max)())
            {
                operation_error = error("keyctl_get_persistent", EOVERFLOW);
                return false;
            }

            keyring = static_cast<key_serial_t>(result);
            return true;
        }

        long search_key(
            const keyutils_api& keyutils,
            key_serial_t keyring,
            const std::string& signature) noexcept
        {
            errno = 0;
            return keyutils.search(
                keyring,
                key_type,
                signature.c_str(),
                0);
        }
    }

    static SecretResult get_secret_in_scope(
        const std::string& signature,
        storage_scope scope)
    {
        SecretError operation_error;
        if (!validate_signature(signature, operation_error))
        {
            return {
                SecretStatus::failed,
                {},
                std::move(operation_error),
            };
        }

        const keyutils_api* keyutils = api(operation_error);
        if (keyutils == nullptr)
        {
            return {
                SecretStatus::failed,
                {},
                std::move(operation_error),
            };
        }

        key_serial_t keyring = 0;
        if (!storage_keyring(*keyutils, scope, keyring, operation_error))
        {
            return {
                SecretStatus::failed,
                {},
                std::move(operation_error),
            };
        }

        const long key = search_key(*keyutils, keyring, signature);
        if (key < 0)
        {
            const int code = errno == 0 ? EIO : errno;
            return {
                code == ENOKEY ? SecretStatus::not_found : SecretStatus::failed,
                {},
                error("keyctl_search", code),
            };
        }

        errno = 0;
        long required = keyutils->read(
            static_cast<key_serial_t>(key),
            nullptr,
            0);
        if (required < 0)
        {
            return {
                SecretStatus::failed,
                {},
                error("keyctl_read(size)", errno == 0 ? EIO : errno),
            };
        }

        std::string value(static_cast<std::size_t>(required), '\0');
        for (;;)
        {
            errno = 0;
            const long bytes = keyutils->read(
                static_cast<key_serial_t>(key),
                value.empty() ? nullptr : value.data(),
                value.size());
            if (bytes < 0)
            {
                return {
                    SecretStatus::failed,
                    {},
                    error("keyctl_read", errno == 0 ? EIO : errno),
                };
            }

            const std::size_t size = static_cast<std::size_t>(bytes);
            if (size <= value.size())
            {
                value.resize(size);
                break;
            }

            value.resize(size);
        }

        return {
            SecretStatus::success,
            std::move(value),
            {},
        };
    }

    SecretResult get_secret(const std::string& signature)
    {
        return get_secret_in_scope(signature, storage_scope::persistent);
    }

    SecretResult get_session_secret(const std::string& signature)
    {
        return get_secret_in_scope(signature, storage_scope::session);
    }

    static SecretOperationResult set_secret_in_scope(
        const std::string& signature,
        const std::string& value,
        storage_scope scope)
    {
        SecretError operation_error;
        if (!validate_signature(signature, operation_error)
            || !validate_value(value, operation_error))
        {
            return {
                SecretStatus::failed,
                std::move(operation_error),
            };
        }

        const keyutils_api* keyutils = api(operation_error);
        if (keyutils == nullptr)
        {
            return {
                SecretStatus::failed,
                std::move(operation_error),
            };
        }

        key_serial_t keyring = 0;
        if (!storage_keyring(*keyutils, scope, keyring, operation_error))
        {
            return {
                SecretStatus::failed,
                std::move(operation_error),
            };
        }

        const long key = search_key(*keyutils, keyring, signature);
        if (key >= 0)
        {
            errno = 0;
            if (keyutils->update(
                    static_cast<key_serial_t>(key),
                    value.data(),
                    value.size()) < 0)
            {
                return {
                    SecretStatus::failed,
                    error("keyctl_update", errno == 0 ? EIO : errno),
                };
            }

            return {SecretStatus::success, {}};
        }

        const int search_error = errno == 0 ? EIO : errno;
        if (search_error != ENOKEY)
        {
            return {
                SecretStatus::failed,
                error("keyctl_search", search_error),
            };
        }

        errno = 0;
        if (keyutils->add_key(
                key_type,
                signature.c_str(),
                value.data(),
                value.size(),
                keyring) < 0)
        {
            return {
                SecretStatus::failed,
                error("add_key", errno == 0 ? EIO : errno),
            };
        }

        return {SecretStatus::success, {}};
    }

    SecretOperationResult set_secret(
        const std::string& signature,
        const std::string& value)
    {
        return set_secret_in_scope(
            signature,
            value,
            storage_scope::persistent);
    }

    SecretOperationResult set_session_secret(
        const std::string& signature,
        const std::string& value)
    {
        return set_secret_in_scope(
            signature,
            value,
            storage_scope::session);
    }

    static SecretOperationResult erase_secret_in_scope(
        const std::string& signature,
        storage_scope scope)
    {
        SecretError operation_error;
        if (!validate_signature(signature, operation_error))
        {
            return {
                SecretStatus::failed,
                std::move(operation_error),
            };
        }

        const keyutils_api* keyutils = api(operation_error);
        if (keyutils == nullptr)
        {
            return {
                SecretStatus::failed,
                std::move(operation_error),
            };
        }

        key_serial_t keyring = 0;
        if (!storage_keyring(*keyutils, scope, keyring, operation_error))
        {
            return {
                SecretStatus::failed,
                std::move(operation_error),
            };
        }

        const long key = search_key(*keyutils, keyring, signature);
        if (key < 0)
        {
            const int code = errno == 0 ? EIO : errno;
            return {
                code == ENOKEY ? SecretStatus::not_found : SecretStatus::failed,
                error("keyctl_search", code),
            };
        }

        errno = 0;
        if (keyutils->unlink(
                static_cast<key_serial_t>(key),
                keyring) < 0)
        {
            return {
                SecretStatus::failed,
                error("keyctl_unlink", errno == 0 ? EIO : errno),
            };
        }

        return {SecretStatus::success, {}};
    }

    SecretOperationResult erase_secret(const std::string& signature)
    {
        return erase_secret_in_scope(signature, storage_scope::persistent);
    }

    SecretOperationResult erase_session_secret(const std::string& signature)
    {
        return erase_secret_in_scope(signature, storage_scope::session);
    }
}
