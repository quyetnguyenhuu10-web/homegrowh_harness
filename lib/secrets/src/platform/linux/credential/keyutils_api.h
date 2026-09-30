#pragma once

#include <secrets>

#include <cstdint>
#include <memory>
#include <sys/types.h>

namespace secrets::linux::detail
{
    using key_serial_t = std::int32_t;
    inline constexpr key_serial_t session_keyring = -3;
    inline constexpr key_serial_t user_keyring = -4;
    inline constexpr const char* key_type = "user";

    struct LibraryDeleter
    {
        void operator()(void* library) const noexcept;
    };
    using UniqueLibrary = std::unique_ptr<void, LibraryDeleter>;

    struct KeyutilsApi
    {
        UniqueLibrary library;
        key_serial_t (*add_key)(const char*, const char*, const void*, std::size_t, key_serial_t) = nullptr;
        long (*get_persistent)(uid_t, key_serial_t) = nullptr;
        long (*search)(key_serial_t, const char*, const char*, key_serial_t) = nullptr;
        long (*read)(key_serial_t, char*, std::size_t) = nullptr;
        long (*update)(key_serial_t, const void*, std::size_t) = nullptr;
        long (*unlink)(key_serial_t, key_serial_t) = nullptr;
    };

    Result<const KeyutilsApi*> keyutils_api();
}
