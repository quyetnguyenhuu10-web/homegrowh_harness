#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <memory>

namespace sandbox::detail::process::windows
{
    struct handle_closer
    {
        void operator()(void* value) const noexcept
        {
            if (value != nullptr && value != INVALID_HANDLE_VALUE)
                CloseHandle(static_cast<HANDLE>(value));
        }
    };

    using unique_handle = std::unique_ptr<void, handle_closer>;

    inline unique_handle own_handle(HANDLE handle) noexcept
    {
        return unique_handle(handle);
    }

    struct local_free_deleter
    {
        void operator()(void* value) const noexcept
        {
            if (value != nullptr)
                LocalFree(value);
        }
    };

    using unique_local_memory = std::unique_ptr<void, local_free_deleter>;

    struct free_sid_deleter
    {
        void operator()(void* value) const noexcept
        {
            if (value != nullptr)
                FreeSid(value);
        }
    };

    using unique_sid = std::unique_ptr<void, free_sid_deleter>;
}
