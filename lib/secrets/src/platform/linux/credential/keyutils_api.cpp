#include "keyutils_api.h"
#include "error/error.h"

#include <dlfcn.h>

#include <utility>

namespace secrets::linux::detail
{
    namespace
    {
        Error loader_error(
            const char* operation,
            const char* api,
            const char* message,
            nlohmann::json&& payload)
        {
            payload["api"] = api;
            return secrets::detail::make_error(
                operation, "dependency_error", message == nullptr ? "" : message,
                {std::move(payload)});
        }

        SecretOperationResult close_library(void* library)
        {
            if (library != nullptr && dlclose(library) != 0)
            {
                const char* message = dlerror();
                return SecretOperationResult::failure(
                    loader_error("close_keyutils", "dlclose", message, {}));
            }
            return SecretOperationResult::success(std::monostate{});
        }

        Result<UniqueLibrary> open_library()
        {
            std::vector<Error> failures;
            for (const char* name : {"libkeyutils.so.1", "libkeyutils.so"})
            {
                void* raw = dlopen(name, RTLD_NOW | RTLD_LOCAL);
                if (raw != nullptr)
                    return Result<UniqueLibrary>::success(UniqueLibrary(raw));
                const char* message = dlerror();
                failures.push_back(loader_error(
                    "load_keyutils", "dlopen", message, {{"library", name}}));
            }
            return Result<UniqueLibrary>::failure(secrets::detail::make_error(
                "load_keyutils", "dependency_error", "Credential backend could not be loaded",
                {}, std::move(failures)));
        }

        template <typename Function>
        Result<Function> symbol(void* library, const char* name)
        {
            (void)dlerror();
            void* address = dlsym(library, name);
            const char* message = dlerror();
            if (message != nullptr || address == nullptr)
            {
                return Result<Function>::failure(
                    loader_error("load_keyutils", "dlsym", message, {{"symbol", name}}));
            }
            return Result<Function>::success(reinterpret_cast<Function>(address));
        }

        template <typename Function>
        void load_required(
            void* library,
            const char* name,
            Function& target,
            std::vector<Error>& failures)
        {
            auto result = symbol<Function>(library, name);
            if (result.error)
                failures.push_back(std::move(*result.error));
            else
                target = *result.value;
        }

        Result<KeyutilsApi> load_keyutils()
        {
            return secrets::detail::guard<KeyutilsApi>("load_keyutils", []
            {
                auto opened = open_library();
                if (opened.error)
                    return Result<KeyutilsApi>::failure(std::move(*opened.error));
                KeyutilsApi loaded;
                loaded.library = std::move(*opened.value);
                std::vector<Error> failures;
                void* library = loaded.library.get();
                load_required(library, "add_key", loaded.add_key, failures);
                load_required(library, "keyctl_search", loaded.search, failures);
                load_required(library, "keyctl_read", loaded.read, failures);
                load_required(library, "keyctl_update", loaded.update, failures);
                load_required(library, "keyctl_unlink", loaded.unlink, failures);
                auto persistent = symbol<decltype(loaded.get_persistent)>(library, "keyctl_get_persistent");
                if (persistent.value)
                    loaded.get_persistent = *persistent.value;

                if (!failures.empty())
                {
                    auto cleanup = close_library(loaded.library.release());
                    if (cleanup.error)
                        failures.push_back(std::move(*cleanup.error));
                    return Result<KeyutilsApi>::failure(secrets::detail::make_error(
                        "load_keyutils", "dependency_error",
                        "Credential backend has missing entry points", {}, std::move(failures)));
                }
                return Result<KeyutilsApi>::success(std::move(loaded));
            });
        }
    }

    void LibraryDeleter::operator()(void* library) const noexcept
    {
        // Explicit loader failure paths retain cleanup errors in causes.
        // At process teardown there is no caller to receive a cleanup result.
        try
        {
            const SecretOperationResult cleanup = close_library(library);
            (void)cleanup;
        }
        catch (...)
        {
            const std::exception_ptr exception = std::current_exception();
            (void)exception;
        }
    }

    Result<const KeyutilsApi*> keyutils_api()
    {
        static const auto shared = load_keyutils();
        if (shared.error)
            return Result<const KeyutilsApi*>::failure(Error(*shared.error));
        return Result<const KeyutilsApi*>::success(&*shared.value);
    }
}
