#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <memory>

namespace fsystem::windows::detail
{
    struct handle_deleter
    {
        using pointer = HANDLE;

        void operator()(pointer handle) const noexcept
        {
            if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
                return;

            (void)CloseHandle(handle);
        }
    };

    using unique_handle = std::unique_ptr<void, handle_deleter>;
}
