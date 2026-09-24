#include "completion.h"
#include "request/requests.h"

#include <Windows.h>

#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>

namespace provider::windows
{
    namespace
    {
        constexpr ULONG_PTR completion_state_mask = 3;
        constexpr ULONG_PTR finished_state = 1;
        constexpr ULONG_PTR failed_state = 2;

        HANDLE native_handle(std::uintptr_t handle) noexcept
        {
            return reinterpret_cast<HANDLE>(handle);
        }

        void post(
            std::uintptr_t producer_handle,
            RawResponse* response,
            std::size_t bytes,
            ULONG_PTR state)
        {
            if (producer_handle == 0)
            {
                return;
            }

            if (bytes > (std::numeric_limits<DWORD>::max)())
            {
                throw std::runtime_error("provider completion is too large");
            }

            static_assert(alignof(RawResponse) >= 4);

            ULONG_PTR key = reinterpret_cast<ULONG_PTR>(response);
            key |= state;

            if (PostQueuedCompletionStatus(
                    native_handle(producer_handle),
                    static_cast<DWORD>(bytes),
                    key,
                    nullptr) == FALSE)
            {
                const DWORD error = GetLastError();

                throw std::system_error(
                    static_cast<int>(error),
                    std::system_category());
            }
        }
    }

    void completion_create(std::uintptr_t (&handles)[2])
    {
        HANDLE handle = CreateIoCompletionPort(
            INVALID_HANDLE_VALUE,
            nullptr,
            0,
            1);

        if (handle == nullptr)
        {
            const DWORD error = GetLastError();

            throw std::system_error(
                static_cast<int>(error),
                std::system_category());
        }

        handles[0] = reinterpret_cast<std::uintptr_t>(handle);
        handles[1] = 0;
    }

    void completion_destroy(std::uintptr_t (&handles)[2]) noexcept
    {
        if (handles[0] != 0)
        {
            CloseHandle(native_handle(handles[0]));
        }

        handles[0] = 0;
        handles[1] = 0;
    }

    std::uintptr_t completion_producer_handle(
        const std::uintptr_t (&handles)[2]) noexcept
    {
        return handles[0];
    }

    void completion_post_data(
        std::uintptr_t producer_handle,
        RawResponse* response,
        std::size_t bytes)
    {
        post(producer_handle, response, bytes, 0);
    }

    void completion_post_finished(
        std::uintptr_t producer_handle,
        RawResponse* response)
    {
        post(producer_handle, response, 0, finished_state);
    }

    void completion_post_failed(
        std::uintptr_t producer_handle,
        RawResponse* response,
        std::uint32_t error)
    {
        post(producer_handle, response, error, failed_state);
    }

    bool completion_wait(
        const std::uintptr_t (&handles)[2],
        detail::NativeCompletion* completion,
        std::uint32_t timeout_ms)
    {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;

        if (GetQueuedCompletionStatus(
                native_handle(handles[0]),
                &bytes,
                &key,
                &overlapped,
                static_cast<DWORD>(timeout_ms)) == FALSE)
        {
            const DWORD error = GetLastError();

            if (error == WAIT_TIMEOUT)
            {
                return false;
            }

            completion->type = detail::NativeCompletionType::failed;
            completion->response = nullptr;
            completion->bytes = 0;
            completion->error = static_cast<std::uint32_t>(error);
            return true;
        }

        const ULONG_PTR state = key & completion_state_mask;

        completion->type =
            state == finished_state
                ? detail::NativeCompletionType::finished
                : state == failed_state
                    ? detail::NativeCompletionType::failed
                    : detail::NativeCompletionType::data;
        completion->response = reinterpret_cast<RawResponse*>(
            key & ~completion_state_mask);
        completion->bytes = state == 0
            ? static_cast<std::size_t>(bytes)
            : 0;
        completion->error = state == failed_state
            ? static_cast<std::uint32_t>(bytes)
            : 0;
        return true;
    }
}
