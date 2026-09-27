#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include "../sandbox/run.h"
#include "process/job.h"
#include "process/raii.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace sandbox::detail
{
    namespace
    {
        struct pipe_pair
        {
            windows::unique_handle read;
            windows::unique_handle write;
        };

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

        pipe_pair create_pipe()
        {
            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;

            HANDLE raw_read = nullptr;
            HANDLE raw_write = nullptr;
            if (!CreatePipe(&raw_read, &raw_write, &security, 0))
                throw_last_error("CreatePipe(where powershell)");

            pipe_pair pipe{
                windows::own_handle(raw_read),
                windows::own_handle(raw_write),
            };

            if (!SetHandleInformation(
                    static_cast<HANDLE>(pipe.read.get()),
                    HANDLE_FLAG_INHERIT,
                    0))
            {
                throw_last_error("SetHandleInformation(where powershell)");
            }
            return pipe;
        }

        std::wstring read_pipe(HANDLE handle)
        {
            std::string bytes;
            std::array<char, 4096> buffer{};
            for (;;)
            {
                DWORD read = 0;
                if (!ReadFile(
                        handle,
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &read,
                        nullptr))
                {
                    const DWORD error = GetLastError();
                    if (error == ERROR_BROKEN_PIPE)
                        break;
                    throw std::system_error(
                        static_cast<int>(error),
                        std::system_category(),
                        "ReadFile(where powershell)");
                }
                if (read == 0)
                    break;
                bytes.append(buffer.data(), read);
            }

            if (bytes.empty())
                return {};

            const int required = MultiByteToWideChar(
                CP_ACP,
                0,
                bytes.data(),
                static_cast<int>(bytes.size()),
                nullptr,
                0);
            if (required <= 0)
                throw_last_error("MultiByteToWideChar(where powershell)");

            std::wstring output(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(
                    CP_ACP,
                    0,
                    bytes.data(),
                    static_cast<int>(bytes.size()),
                    output.data(),
                    required) <= 0)
            {
                throw_last_error("MultiByteToWideChar(where powershell)");
            }
            return output;
        }

        std::wstring resolve_powershell()
        {
            pipe_pair output = create_pipe();

            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;
            windows::unique_handle null_input = windows::own_handle(CreateFileW(
                L"NUL",
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                &security,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr));
            if (!null_input)
                throw_last_error("CreateFileW(NUL for where powershell)");

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            startup.hStdInput = static_cast<HANDLE>(null_input.get());
            startup.hStdOutput = static_cast<HANDLE>(output.write.get());
            startup.hStdError = static_cast<HANDLE>(output.write.get());

            std::wstring command = L"where powershell";
            command.push_back(L'\0');

            PROCESS_INFORMATION information{};
            if (!CreateProcessW(
                    nullptr,
                    command.data(),
                    nullptr,
                    nullptr,
                    TRUE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    nullptr,
                    &startup,
                    &information))
            {
                throw_last_error("CreateProcessW(where powershell)");
            }

            windows::unique_handle process = windows::own_handle(information.hProcess);
            windows::unique_handle thread = windows::own_handle(information.hThread);
            output.write.reset();

            if (WaitForSingleObject(information.hProcess, INFINITE) == WAIT_FAILED)
                throw_last_error("WaitForSingleObject(where powershell)");

            DWORD exit_code = 0;
            if (!GetExitCodeProcess(information.hProcess, &exit_code))
                throw_last_error("GetExitCodeProcess(where powershell)");
            const std::wstring text = read_pipe(static_cast<HANDLE>(output.read.get()));
            if (exit_code != 0)
            {
                throw std::system_error(
                    std::make_error_code(std::errc::no_such_file_or_directory),
                    "where powershell did not find PowerShell");
            }

            const std::size_t begin = text.find_first_not_of(L" \t\r\n");
            if (begin == std::wstring::npos)
            {
                throw std::system_error(
                    std::make_error_code(std::errc::no_such_file_or_directory),
                    "where powershell returned no path");
            }
            const std::size_t end = text.find_first_of(L"\r\n", begin);
            return text.substr(begin, end == std::wstring::npos ? end : end - begin);
        }

        std::wstring utf8_to_wide(std::string_view body)
        {
            if (body.empty())
                return {};

            const int required = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                body.data(),
                static_cast<int>(body.size()),
                nullptr,
                0);
            if (required <= 0)
                throw_last_error("MultiByteToWideChar(sandbox body)");

            std::wstring output(static_cast<std::size_t>(required), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    body.data(),
                    static_cast<int>(body.size()),
                    output.data(),
                    required) <= 0)
            {
                throw_last_error("MultiByteToWideChar(sandbox body)");
            }
            return output;
        }

        std::string base64_encode(const unsigned char* data, std::size_t size)
        {
            static constexpr char alphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string output;
            output.reserve(((size + 2) / 3) * 4);

            std::size_t index = 0;
            while (index + 3 <= size)
            {
                const std::uint32_t value =
                    (static_cast<std::uint32_t>(data[index]) << 16)
                    | (static_cast<std::uint32_t>(data[index + 1]) << 8)
                    | static_cast<std::uint32_t>(data[index + 2]);
                output.push_back(alphabet[(value >> 18) & 0x3f]);
                output.push_back(alphabet[(value >> 12) & 0x3f]);
                output.push_back(alphabet[(value >> 6) & 0x3f]);
                output.push_back(alphabet[value & 0x3f]);
                index += 3;
            }

            const std::size_t remaining = size - index;
            if (remaining == 1)
            {
                const std::uint32_t value = static_cast<std::uint32_t>(data[index]) << 16;
                output.push_back(alphabet[(value >> 18) & 0x3f]);
                output.push_back(alphabet[(value >> 12) & 0x3f]);
                output.append("==");
            }
            else if (remaining == 2)
            {
                const std::uint32_t value =
                    (static_cast<std::uint32_t>(data[index]) << 16)
                    | (static_cast<std::uint32_t>(data[index + 1]) << 8);
                output.push_back(alphabet[(value >> 18) & 0x3f]);
                output.push_back(alphabet[(value >> 12) & 0x3f]);
                output.push_back(alphabet[(value >> 6) & 0x3f]);
                output.push_back('=');
            }
            return output;
        }

        std::wstring encoded_command(std::string_view body)
        {
            const std::wstring wide = utf8_to_wide(body);
            const auto* bytes = reinterpret_cast<const unsigned char*>(wide.data());
            const std::string encoded = base64_encode(
                bytes,
                wide.size() * sizeof(wchar_t));
            return std::wstring(encoded.begin(), encoded.end());
        }

    }

    int run_body(std::string_view body)
    {
        windows::job process_tree;
        const std::wstring powershell = resolve_powershell();
        const std::wstring encoded = encoded_command(body);
        std::wstring command = L"\"" + powershell
            + L"\" -NoLogo -NoProfile -NonInteractive -EncodedCommand "
            + encoded;
        command.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION information{};

        if (!CreateProcessW(
                powershell.c_str(),
                command.data(),
                nullptr,
                nullptr,
                FALSE,
                CREATE_SUSPENDED,
                nullptr,
                nullptr,
                &startup,
                &information))
        {
            throw std::system_error(last_error(), "CreateProcessW(sandbox powershell)");
        }

        windows::unique_handle process = windows::own_handle(information.hProcess);
        windows::unique_handle thread = windows::own_handle(information.hThread);
        windows::suspended_process_guard suspended(
            static_cast<HANDLE>(process.get()));

        process_tree.assign(static_cast<HANDLE>(process.get()));

        if (ResumeThread(static_cast<HANDLE>(thread.get())) == static_cast<DWORD>(-1))
            throw std::system_error(last_error(), "ResumeThread(sandbox powershell)");
        suspended.release();

        const DWORD wait = WaitForSingleObject(information.hProcess, INFINITE);
        if (wait == WAIT_FAILED)
            throw std::system_error(last_error(), "WaitForSingleObject(sandbox powershell)");

        DWORD exit_code = 0;
        if (!GetExitCodeProcess(information.hProcess, &exit_code))
            throw std::system_error(last_error(), "GetExitCodeProcess(sandbox powershell)");

        return static_cast<int>(exit_code);
    }
}
