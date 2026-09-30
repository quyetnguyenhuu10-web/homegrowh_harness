#include "platform/linux/credential/linux_credential.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/types.h>

namespace
{
    std::string scenario;
    std::string loader_message;
    bool loader_error_pending = false;
    int closes = 0;
    int reads = 0;
    const std::string payload("secret\0bytes", 12);

    void require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    void set_loader_error(std::string&& message)
    {
        loader_message = std::move(message);
        loader_error_pending = true;
    }

    long fake_persistent(uid_t, std::int32_t)
    {
        if (scenario == "storage_failure")
        {
            errno = EPERM;
            return -1;
        }
        return 88;
    }

    long fake_search(std::int32_t, const char*, const char*, std::int32_t)
    {
        if (scenario == "search_failure")
        {
            errno = EACCES;
            return -1;
        }
        if (scenario == "add_failure" || scenario == "not_found")
        {
            errno = ENOKEY;
            return -1;
        }
        return 42;
    }

    long fake_read(std::int32_t, char* buffer, std::size_t size)
    {
        ++reads;
        if (scenario == "read_failure" || (scenario == "read_data_failure" && buffer != nullptr))
        {
            errno = EIO;
            return -1;
        }
        if (scenario == "zero_errno")
        {
            errno = 0;
            return -1;
        }
        if (scenario == "grow_read" && reads == 1)
            return 2;
        if (size >= payload.size())
            std::memcpy(buffer, payload.data(), payload.size());
        return static_cast<long>(payload.size());
    }

    long fake_update(std::int32_t, const void*, std::size_t)
    {
        errno = EIO;
        return -1;
    }

    std::int32_t fake_add(const char*, const char*, const void*, std::size_t, std::int32_t)
    {
        errno = EPERM;
        return -1;
    }

    long fake_unlink(std::int32_t, std::int32_t)
    {
        errno = EBUSY;
        return -1;
    }

    void require_schema(const secrets::Error& error)
    {
        const nlohmann::json json = error;
        require(json.size() == 6 && json.at("data").is_array() && json.at("causes").is_array(),
            "Linux backend did not retain the unified envelope");
        require(nlohmann::json::parse(json.dump()) == json, "Linux Error lost information in serialization");
        for (const auto& cause : error.causes)
            require_schema(cause);
    }

    void require_native(const secrets::Error& error, const char* api, int code)
    {
        require_schema(error);
        const auto& data = error.data.front();
        require(data.at("api") == api, "Linux backend lost the native API");
        require(data.at("errno") == code && data.at("code") == code, "Linux backend invented or lost errno");
        require(data.at("result") == -1, "Linux backend lost the raw failure result");
    }
}

extern "C" void* dlopen(const char* name, int)
{
    if (scenario == "loader_failure")
    {
        set_loader_error(std::string("native dlopen failure: ") + name);
        return nullptr;
    }
    if (scenario == "library_fallback" && std::string(name) == "libkeyutils.so.1")
    {
        set_loader_error("native versioned library failure");
        return nullptr;
    }
    return reinterpret_cast<void*>(std::uintptr_t{1});
}

extern "C" const char* dlerror()
{
    if (!loader_error_pending)
        return nullptr;
    loader_error_pending = false;
    return loader_message.c_str();
}

extern "C" void* dlsym(void*, const char* name)
{
    const std::string symbol(name);
    if (scenario == "symbol_failure" && (symbol == "keyctl_search" || symbol == "keyctl_read"))
    {
        set_loader_error(std::string("native dlsym failure: ") + name);
        return nullptr;
    }
    if (scenario == "optional_fallback" && symbol == "keyctl_get_persistent")
    {
        set_loader_error("native optional symbol missing");
        return nullptr;
    }
    if (symbol == "add_key") return reinterpret_cast<void*>(&fake_add);
    if (symbol == "keyctl_get_persistent") return reinterpret_cast<void*>(&fake_persistent);
    if (symbol == "keyctl_search") return reinterpret_cast<void*>(&fake_search);
    if (symbol == "keyctl_read") return reinterpret_cast<void*>(&fake_read);
    if (symbol == "keyctl_update") return reinterpret_cast<void*>(&fake_update);
    if (symbol == "keyctl_unlink") return reinterpret_cast<void*>(&fake_unlink);
    set_loader_error(std::string("unexpected symbol: ") + name);
    return nullptr;
}

extern "C" int dlclose(void*)
{
    ++closes;
    if (scenario == "symbol_failure")
    {
        set_loader_error("native dlclose failure");
        return -1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    scenario = argv[1];
    try
    {
        if (scenario == "update_failure" || scenario == "add_failure")
        {
            const auto result = secrets::linux::set_secret("signature", "value");
            require(result.error && !result.value, "Linux set failure did not return Error");
            require_native(*result.error,
                scenario == "add_failure" ? "add_key" : "keyctl_update",
                scenario == "add_failure" ? EPERM : EIO);
        }
        else if (scenario == "unlink_failure")
        {
            const auto result = secrets::linux::erase_secret("signature");
            require(result.error && !result.value, "Linux erase failure did not return Error");
            require_native(*result.error, "keyctl_unlink", EBUSY);
        }
        else
        {
            const auto result = secrets::linux::get_secret("signature");
            if (scenario == "grow_read" || scenario == "optional_fallback" || scenario == "library_fallback")
            {
                require(result.value && !result.error, "Handled fallback must succeed");
                require(*result.value == payload, "Linux read lost credential bytes");
            }
            else
            {
                require(result.error && !result.value, "Linux get failure did not return Error");
                require_schema(*result.error);
                if (scenario == "loader_failure")
                {
                    require(result.error->causes.size() == 2, "Independent dlopen failures were lost");
                    require(result.error->causes[0].message ==
                        "native dlopen failure: libkeyutils.so.1", "First loader error was replaced");
                    require(result.error->causes[1].message ==
                        "native dlopen failure: libkeyutils.so", "Second loader error was replaced");
                    require(!result.error->causes.front().data.front().contains("code"),
                        "Loader error invented an errno code");
                }
                else if (scenario == "symbol_failure")
                {
                    require(result.error->causes.size() == 3, "Symbol and cleanup failures were lost");
                    require(result.error->causes[0].message == "native dlsym failure: keyctl_search",
                        "First symbol error was replaced");
                    require(result.error->causes[1].message == "native dlsym failure: keyctl_read",
                        "Second symbol error was replaced");
                    require(result.error->causes[2].message == "native dlclose failure",
                        "Cleanup error was replaced");
                    require(closes == 1, "Failed loader did not release its library exactly once");
                }
                else if (scenario == "storage_failure")
                    require_native(*result.error, "keyctl_get_persistent", EPERM);
                else if (scenario == "search_failure")
                    require_native(*result.error, "keyctl_search", EACCES);
                else if (scenario == "not_found")
                {
                    require_native(*result.error, "keyctl_search", ENOKEY);
                    require(result.error->type == "not_found", "Missing key has wrong semantic type");
                }
                else
                    require_native(*result.error, "keyctl_read", scenario == "zero_errno" ? 0 : EIO);
            }
        }
        std::cout << "Linux backend " << scenario << " passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
