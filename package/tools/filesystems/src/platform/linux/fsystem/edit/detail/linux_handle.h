#pragma once

#include <cstddef>
#include <memory>
#include <unistd.h>

namespace fsystem::linux::detail
{
    class fd_handle
    {
    public:
        constexpr fd_handle() noexcept = default;
        constexpr fd_handle(std::nullptr_t) noexcept {}

        explicit constexpr fd_handle(int value) noexcept
            : value_(value)
        {
        }

        constexpr int get() const noexcept
        {
            return value_;
        }

        constexpr explicit operator bool() const noexcept
        {
            return value_ >= 0;
        }

        friend constexpr bool operator==(
            fd_handle lhs,
            fd_handle rhs
        ) noexcept
        {
            return lhs.value_ == rhs.value_;
        }

        friend constexpr bool operator==(
            fd_handle lhs,
            std::nullptr_t
        ) noexcept
        {
            return lhs.value_ < 0;
        }

        friend constexpr bool operator==(
            std::nullptr_t,
            fd_handle rhs
        ) noexcept
        {
            return rhs == nullptr;
        }

        friend constexpr bool operator!=(
            fd_handle lhs,
            std::nullptr_t
        ) noexcept
        {
            return !(lhs == nullptr);
        }

        friend constexpr bool operator!=(
            std::nullptr_t,
            fd_handle rhs
        ) noexcept
        {
            return !(rhs == nullptr);
        }

    private:
        int value_{-1};
    };

    struct fd_deleter
    {
        using pointer = fd_handle;

        void operator()(pointer handle) const noexcept
        {
            if (handle != nullptr)
                (void)::close(handle.get());
        }
    };

    using unique_fd = std::unique_ptr<int, fd_deleter>;
}
