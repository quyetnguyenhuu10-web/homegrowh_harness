#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <memory>

namespace sandbox::detail::windows
{
    struct handle_closer final
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

    class suspended_process_guard final
    {
    public:
        explicit suspended_process_guard(HANDLE process) noexcept
            : process_(process)
        {
        }

        suspended_process_guard(const suspended_process_guard&) = delete;
        suspended_process_guard& operator=(const suspended_process_guard&) = delete;

        ~suspended_process_guard()
        {
            if (active_ && process_ != nullptr)
                TerminateProcess(process_, ERROR_PROCESS_ABORTED);
        }

        void release() noexcept
        {
            active_ = false;
        }

    private:
        HANDLE process_ = nullptr;
        bool active_ = true;
    };
}
