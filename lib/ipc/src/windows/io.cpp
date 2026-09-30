#include "io.h"

#include <algorithm>
#include <limits>

namespace ipc::detail
{
    Error win32_error(
        std::string_view operation, DWORD code,
        std::string_view api, nlohmann::json&& context)
    {
        context["code"] = code;
        context["category"] = "system";
        context["api"] = api;
        return make_error(operation, "system_error",
            std::error_code(static_cast<int>(code), std::system_category()).message(),
            std::move(context));
    }

    namespace
    {
        bool peer_closed_error(DWORD code) noexcept
        {
            return code == ERROR_BROKEN_PIPE || code == ERROR_NO_DATA
                || code == ERROR_PIPE_NOT_CONNECTED;
        }

        struct transfer_result
        {
            DWORD value = 0;
            bool closed = false;
            std::optional<Error> error;
        };

        transfer_result transfer(
            HANDLE handle, void* buffer, DWORD size, bool overlapped, bool writing,
            nlohmann::json&& context)
        {
            const std::string_view operation = writing ? "write" : "read";
            const std::string_view api = writing ? "WriteFile" : "ReadFile";
            unique_handle event;
            OVERLAPPED pending{};
            if (overlapped)
            {
                const HANDLE created = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (created == nullptr)
                {
                    const DWORD code = GetLastError();
                    return {0, false, win32_error(operation, code, "CreateEventW", std::move(context))};
                }
                event = unique_handle(created);
                pending.hEvent = event.get();
            }

            DWORD transferred = 0;
            const BOOL started = writing
                ? WriteFile(handle, buffer, size, overlapped ? nullptr : &transferred,
                    overlapped ? &pending : nullptr)
                : ReadFile(handle, buffer, size, overlapped ? nullptr : &transferred,
                    overlapped ? &pending : nullptr);

            transfer_result result;
            if (!started)
            {
                const DWORD code = GetLastError();
                if (!overlapped || code != ERROR_IO_PENDING)
                {
                    result.closed = !writing && peer_closed_error(code);
                    result.error = win32_error(operation, code, api, nlohmann::json(context));
                }
            }
            if (!result.error && overlapped)
            {
                if (!GetOverlappedResult(handle, &pending, &transferred, TRUE))
                {
                    const DWORD code = GetLastError();
                    result.closed = !writing && peer_closed_error(code);
                    result.error = win32_error(operation, code, "GetOverlappedResult", nlohmann::json(context));
                }
            }
            result.value = result.error ? 0 : transferred;
            const DWORD close_code = event.close();
            if (close_code != ERROR_SUCCESS)
            {
                add_cleanup_error(result.error,
                    win32_error(operation, close_code, "CloseHandle", std::move(context)));
                result.closed = false;
                result.value = 0;
            }
            return result;
        }
    }

    exact_read_result read_exact(
        HANDLE handle, void* output, std::size_t size, bool overlapped,
        const std::string& path, std::string_view phase)
    {
        auto* bytes = static_cast<unsigned char*>(output);
        std::size_t offset = 0;
        while (offset < size)
        {
            const DWORD request = static_cast<DWORD>((std::min)(size - offset,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            auto received = transfer(handle, bytes + offset, request, overlapped, false,
                {{"path", path}, {"phase", phase}, {"expected_size", size}, {"transferred", offset}});
            if (received.closed || (!received.error && received.value == 0))
            {
                if (offset == 0 && phase == "header")
                    return {true, std::nullopt};
                Error error = incomplete_frame("ReadFile", path, phase, size, offset);
                if (received.error)
                    error.causes.push_back(std::move(*received.error));
                return {false, std::move(error)};
            }
            if (received.error)
                return {false, std::move(received.error)};
            offset += received.value;
        }
        return {};
    }

    std::optional<Error> write_exact(
        HANDLE handle, const void* input, std::size_t size, bool overlapped,
        const std::string& path, std::string_view phase)
    {
        const auto* bytes = static_cast<const unsigned char*>(input);
        std::size_t offset = 0;
        while (offset < size)
        {
            const DWORD request = static_cast<DWORD>((std::min)(size - offset,
                static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
            auto written = transfer(handle, const_cast<unsigned char*>(bytes + offset), request, overlapped, true,
                {{"path", path}, {"phase", phase}, {"expected_size", size}, {"transferred", offset}});
            if (written.error)
                return std::move(written.error);
            if (written.value == 0)
            {
                return make_error("write", "protocol_error",
                    "WriteFile transferred zero bytes before the frame was complete",
                    {{"api", "WriteFile"}, {"path", path}, {"phase", phase},
                     {"expected_size", size}, {"transferred", offset}, {"return_value", written.value}});
            }
            offset += written.value;
        }
        return std::nullopt;
    }
}
