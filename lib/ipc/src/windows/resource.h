#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <utility>

namespace ipc::detail
{
    class unique_handle final
    {
    public:
        unique_handle() noexcept = default;
        explicit unique_handle(HANDLE value) noexcept : value_(value) {}
        unique_handle(const unique_handle&) = delete;
        unique_handle& operator=(const unique_handle&) = delete;
        unique_handle(unique_handle&& other) noexcept
            : value_(std::exchange(other.value_, INVALID_HANDLE_VALUE)) {}
        unique_handle& operator=(unique_handle&& other) noexcept
        {
            if (this != &other)
            {
                close();
                value_ = std::exchange(other.value_, INVALID_HANDLE_VALUE);
            }
            return *this;
        }
        ~unique_handle() { close(); }

        HANDLE get() const noexcept { return value_; }
        bool valid() const noexcept
        {
            return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
        }
        DWORD close() noexcept
        {
            if (valid())
            {
                const HANDLE handle = std::exchange(value_, INVALID_HANDLE_VALUE);
                if (!CloseHandle(handle))
                    close_error_ = GetLastError();
                else
                    close_error_ = ERROR_SUCCESS;
            }
            return close_error_;
        }

    private:
        HANDLE value_ = INVALID_HANDLE_VALUE;
        DWORD close_error_ = ERROR_SUCCESS;
    };
}
