#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../ipc/platform.h"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace event_port::ipc::detail
{
    namespace
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

        std::error_code last_error() noexcept
        {
            return {
                static_cast<int>(GetLastError()),
                std::system_category()
            };
        }

        [[noreturn]] void throw_last_error(const char* action)
        {
            throw std::system_error(last_error(), action);
        }

        unique_handle create_event(const char* action)
        {
            unique_handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
            if (!event)
                throw_last_error(action);
            return event;
        }

        std::string make_endpoint()
        {
            static std::atomic<std::uint64_t> sequence{1};
            return "homegrowh.event_port."
                + std::to_string(GetCurrentProcessId())
                + "."
                + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed));
        }

        std::wstring pipe_path(const std::string& endpoint)
        {
            return L"\\\\.\\pipe\\" + std::wstring(endpoint.begin(), endpoint.end());
        }

        unique_handle create_pipe_instance(const std::string& endpoint)
        {
            const std::wstring path = pipe_path(endpoint);
            unique_handle handle(CreateNamedPipeW(
                path.c_str(),
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                PIPE_UNLIMITED_INSTANCES,
                64 * 1024,
                64 * 1024,
                0,
                nullptr));

            if (!handle || handle.get() == INVALID_HANDLE_VALUE)
                throw_last_error("CreateNamedPipeW(event_port ipc)");
            return handle;
        }

        class windows_connection final : public connection_backend
        {
        public:
            explicit windows_connection(unique_handle&& handle) noexcept
                : handle_(std::move(handle))
            {
            }

            std::size_t read(std::span<std::byte> buffer) override
            {
                if (buffer.empty())
                    return 0;

                const DWORD requested = static_cast<DWORD>(
                    (std::min)(
                        buffer.size(),
                        static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
                unique_handle event = create_event("CreateEventW(event_port ipc read)");
                OVERLAPPED overlapped{};
                overlapped.hEvent = static_cast<HANDLE>(event.get());
                if (!ReadFile(
                        static_cast<HANDLE>(handle_.get()),
                        buffer.data(),
                        requested,
                        nullptr,
                        &overlapped))
                {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE)
                        return 0;
                    if (error != ERROR_IO_PENDING)
                    {
                        throw std::system_error(
                            static_cast<int>(error),
                            std::system_category(),
                            "ReadFile(event_port ipc)");
                    }
                    if (WaitForSingleObject(
                            static_cast<HANDLE>(event.get()),
                            INFINITE) == WAIT_FAILED)
                    {
                        throw_last_error("WaitForSingleObject(event_port ipc read)");
                    }
                }

                DWORD received = 0;
                if (!GetOverlappedResult(
                        static_cast<HANDLE>(handle_.get()),
                        &overlapped,
                        &received,
                        FALSE))
                {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE)
                        return 0;
                    throw std::system_error(
                        static_cast<int>(error),
                        std::system_category(),
                        "GetOverlappedResult(event_port ipc read)");
                }
                return static_cast<std::size_t>(received);
            }

            void write(std::span<const std::byte> data) override
            {
                std::size_t offset = 0;
                while (offset < data.size())
                {
                    const DWORD requested = static_cast<DWORD>(
                        (std::min)(
                            data.size() - offset,
                            static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
                    unique_handle event = create_event("CreateEventW(event_port ipc write)");
                    OVERLAPPED overlapped{};
                    overlapped.hEvent = static_cast<HANDLE>(event.get());
                    if (!WriteFile(
                            static_cast<HANDLE>(handle_.get()),
                            data.data() + offset,
                            requested,
                            nullptr,
                            &overlapped))
                    {
                        const DWORD error = GetLastError();
                        if (error != ERROR_IO_PENDING)
                        {
                            throw std::system_error(
                                static_cast<int>(error),
                                std::system_category(),
                                "WriteFile(event_port ipc)");
                        }
                        if (WaitForSingleObject(
                                static_cast<HANDLE>(event.get()),
                                INFINITE) == WAIT_FAILED)
                        {
                            throw_last_error("WaitForSingleObject(event_port ipc write)");
                        }
                    }

                    DWORD written = 0;
                    if (!GetOverlappedResult(
                            static_cast<HANDLE>(handle_.get()),
                            &overlapped,
                            &written,
                            FALSE))
                    {
                        throw_last_error("GetOverlappedResult(event_port ipc write)");
                    }
                    if (written == 0)
                    {
                        throw std::system_error(
                            std::make_error_code(std::errc::io_error),
                            "WriteFile(event_port ipc) wrote zero bytes");
                    }
                    offset += static_cast<std::size_t>(written);
                }
            }

        private:
            unique_handle handle_;
        };

        class windows_listener final : public listener_backend
        {
        public:
            windows_listener()
                : endpoint_(make_endpoint()),
                  listener_(create_pipe_instance(endpoint_))
            {
            }

            const std::string& endpoint() const noexcept override
            {
                return endpoint_;
            }

            std::unique_ptr<connection_backend> accept() override
            {
                return accept_impl(INFINITE);
            }

            std::unique_ptr<connection_backend> accept_for(
                std::chrono::milliseconds timeout) override
            {
                const auto count = timeout.count();
                const DWORD wait_ms = count > static_cast<long long>(INFINITE - 1)
                    ? INFINITE - 1
                    : static_cast<DWORD>(count);
                return accept_impl(wait_ms);
            }

        private:
            std::unique_ptr<connection_backend> accept_impl(DWORD wait_ms)
            {
                unique_handle event = create_event("CreateEventW(event_port ipc accept)");

                OVERLAPPED overlapped{};
                overlapped.hEvent = static_cast<HANDLE>(event.get());
                HANDLE handle = static_cast<HANDLE>(listener_.get());

                bool connected_now = false;
                if (!ConnectNamedPipe(handle, &overlapped))
                {
                    const DWORD error = GetLastError();
                    if (error == ERROR_PIPE_CONNECTED)
                    {
                        connected_now = true;
                    }
                    else if (error != ERROR_IO_PENDING)
                    {
                        throw std::system_error(
                            static_cast<int>(error),
                            std::system_category(),
                            "ConnectNamedPipe(event_port ipc)");
                    }
                }
                else
                {
                    connected_now = true;
                }

                if (!connected_now)
                {
                    const DWORD wait = WaitForSingleObject(
                        static_cast<HANDLE>(event.get()),
                        wait_ms);
                    if (wait == WAIT_TIMEOUT)
                    {
                        if (!CancelIoEx(handle, &overlapped))
                        {
                            const DWORD error = GetLastError();
                            if (error != ERROR_NOT_FOUND)
                            {
                                throw std::system_error(
                                    static_cast<int>(error),
                                    std::system_category(),
                                    "CancelIoEx(event_port ipc accept)");
                            }
                        }
                        const DWORD completion_wait = WaitForSingleObject(
                            static_cast<HANDLE>(event.get()),
                            INFINITE);
                        if (completion_wait == WAIT_FAILED)
                            throw_last_error("WaitForSingleObject(event_port ipc accept cancellation)");

                        DWORD transferred = 0;
                        if (!GetOverlappedResult(
                                handle,
                                &overlapped,
                                &transferred,
                                FALSE))
                        {
                            const DWORD error = GetLastError();
                            if (error == ERROR_OPERATION_ABORTED)
                            {
                                listener_ = create_pipe_instance(endpoint_);
                                return nullptr;
                            }
                            if (error != ERROR_PIPE_CONNECTED)
                            {
                                throw std::system_error(
                                    static_cast<int>(error),
                                    std::system_category(),
                                    "GetOverlappedResult(event_port ipc accept cancellation)");
                            }
                        }

                        connected_now = true;
                    }
                    if (wait == WAIT_FAILED)
                        throw_last_error("WaitForSingleObject(event_port ipc accept)");

                    DWORD transferred = 0;
                    if (!GetOverlappedResult(handle, &overlapped, &transferred, FALSE))
                    {
                        const DWORD error = GetLastError();
                        if (error != ERROR_PIPE_CONNECTED)
                        {
                            throw std::system_error(
                                static_cast<int>(error),
                                std::system_category(),
                                "GetOverlappedResult(event_port ipc accept)");
                        }
                    }
                }

                unique_handle connected = std::move(listener_);
                listener_ = create_pipe_instance(endpoint_);
                return std::make_unique<windows_connection>(std::move(connected));
            }

            std::string endpoint_;
            unique_handle listener_;
        };
    }

    std::unique_ptr<listener_backend> make_listener()
    {
        return std::make_unique<windows_listener>();
    }

    std::unique_ptr<connection_backend> connect(const std::string& endpoint)
    {
        const std::wstring path = pipe_path(endpoint);
        for (;;)
        {
            unique_handle handle(CreateFileW(
                path.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr));
            if (handle && handle.get() != INVALID_HANDLE_VALUE)
                return std::make_unique<windows_connection>(std::move(handle));

            const DWORD error = GetLastError();
            if (error != ERROR_PIPE_BUSY)
            {
                throw std::system_error(
                    static_cast<int>(error),
                    std::system_category(),
                    "CreateFileW(event_port ipc connect)");
            }
            if (!WaitNamedPipeW(path.c_str(), 5000))
                throw_last_error("WaitNamedPipeW(event_port ipc connect)");
        }
    }
}
