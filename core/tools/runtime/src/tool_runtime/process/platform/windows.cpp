#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "windows.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace tool_runtime::detail::process_platform::windows
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

        struct pipe_pair final
        {
            unique_handle read;
            unique_handle write;
        };

        std::error_code error_code(DWORD value) noexcept
        {
            return {static_cast<int>(value), std::system_category()};
        }

        pipe_pair create_pipe(bool parent_reads)
        {
            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;

            HANDLE raw_read = nullptr;
            HANDLE raw_write = nullptr;
            if (!CreatePipe(&raw_read, &raw_write, &security, 0))
                throw std::system_error(error_code(GetLastError()), "CreatePipe(tool runtime)");

            pipe_pair result{
                unique_handle(raw_read),
                unique_handle(raw_write),
            };
            HANDLE parent = static_cast<HANDLE>(
                parent_reads ? result.read.get() : result.write.get());
            if (!SetHandleInformation(parent, HANDLE_FLAG_INHERIT, 0))
            {
                throw std::system_error(
                    error_code(GetLastError()),
                    "SetHandleInformation(tool runtime)");
            }
            return result;
        }

        std::wstring utf8_to_wide(std::string_view value)
        {
            if (value.empty())
                return {};
            const int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (size <= 0)
            {
                throw std::system_error(
                    error_code(GetLastError()),
                    "MultiByteToWideChar(tool runtime)");
            }
            std::wstring output(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    output.data(),
                    size) <= 0)
            {
                throw std::system_error(
                    error_code(GetLastError()),
                    "MultiByteToWideChar(tool runtime)");
            }
            return output;
        }

        std::wstring quote(std::wstring_view value)
        {
            if (!value.empty()
                && value.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
            {
                return std::wstring(value);
            }

            std::wstring output(1, L'\"');
            std::size_t slashes = 0;
            for (const wchar_t ch : value)
            {
                if (ch == L'\\')
                {
                    ++slashes;
                    continue;
                }
                if (ch == L'\"')
                {
                    output.append(slashes * 2 + 1, L'\\');
                    output.push_back(ch);
                    slashes = 0;
                    continue;
                }
                output.append(slashes, L'\\');
                slashes = 0;
                output.push_back(ch);
            }
            output.append(slashes * 2, L'\\');
            output.push_back(L'\"');
            return output;
        }

        std::wstring command_line(const process_request& request)
        {
            std::wstring output = quote(request.executable.wstring());
            for (const std::string& argument : request.arguments)
            {
                output.push_back(L' ');
                output += quote(utf8_to_wide(argument));
            }
            output.push_back(L'\0');
            return output;
        }

        void read_pipe(
            unique_handle handle,
            std::string& output,
            std::atomic<DWORD>& error) noexcept
        {
            std::array<char, 16 * 1024> buffer{};
            for (;;)
            {
                DWORD count = 0;
                if (!ReadFile(
                        static_cast<HANDLE>(handle.get()),
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &count,
                        nullptr))
                {
                    const DWORD value = GetLastError();
                    if (value != ERROR_BROKEN_PIPE)
                        error.store(value, std::memory_order_relaxed);
                    return;
                }
                if (count == 0)
                    return;
                output.append(buffer.data(), count);
            }
        }

        void write_pipe(
            unique_handle handle,
            const std::string& input,
            std::atomic<DWORD>& error) noexcept
        {
            std::size_t offset = 0;
            while (offset < input.size())
            {
                const DWORD count = static_cast<DWORD>(
                    (std::min)(input.size() - offset, static_cast<std::size_t>(0xffffffffu)));
                DWORD written = 0;
                if (!WriteFile(
                        static_cast<HANDLE>(handle.get()),
                        input.data() + offset,
                        count,
                        &written,
                        nullptr))
                {
                    const DWORD value = GetLastError();
                    if (value != ERROR_BROKEN_PIPE && value != ERROR_NO_DATA)
                        error.store(value, std::memory_order_relaxed);
                    return;
                }
                if (written == 0)
                {
                    error.store(ERROR_WRITE_FAULT, std::memory_order_relaxed);
                    return;
                }
                offset += written;
            }
        }
    }

    process_result run(const process_request& request)
    {
        process_result result;
        try
        {
            pipe_pair input = create_pipe(false);
            pipe_pair output = create_pipe(true);
            pipe_pair error_output = create_pipe(true);

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            startup.hStdInput = static_cast<HANDLE>(input.read.get());
            startup.hStdOutput = static_cast<HANDLE>(output.write.get());
            startup.hStdError = static_cast<HANDLE>(error_output.write.get());

            std::wstring command = command_line(request);
            PROCESS_INFORMATION info{};
            if (!CreateProcessW(
                    request.executable.c_str(),
                    command.data(),
                    nullptr,
                    nullptr,
                    TRUE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    request.working_directory.empty()
                        ? nullptr
                        : request.working_directory.c_str(),
                    &startup,
                    &info))
            {
                result.final_error = error_code(GetLastError());
                return result;
            }

            result.started = true;
            unique_handle process(info.hProcess);
            unique_handle thread(info.hThread);
            input.read.reset();
            output.write.reset();
            error_output.write.reset();

            std::atomic<DWORD> io_error{ERROR_SUCCESS};
            std::jthread writer(
                write_pipe,
                std::move(input.write),
                std::cref(request.stdin_data),
                std::ref(io_error));
            std::jthread stdout_reader(
                read_pipe,
                std::move(output.read),
                std::ref(result.stdout_text),
                std::ref(io_error));
            std::jthread stderr_reader(
                read_pipe,
                std::move(error_output.read),
                std::ref(result.stderr_text),
                std::ref(io_error));

            const DWORD wait_ms = request.timeout.count() <= 0
                ? 1u
                : request.timeout.count() > static_cast<long long>(INFINITE - 1)
                    ? INFINITE - 1
                    : static_cast<DWORD>(request.timeout.count());
            const DWORD wait = WaitForSingleObject(info.hProcess, wait_ms);
            if (wait == WAIT_TIMEOUT)
            {
                result.timed_out = true;
                if (!TerminateProcess(info.hProcess, ERROR_TIMEOUT))
                    result.final_error = error_code(GetLastError());
                else
                    result.terminated = true;
                WaitForSingleObject(info.hProcess, INFINITE);
            }
            else if (wait == WAIT_FAILED)
            {
                result.final_error = error_code(GetLastError());
                if (TerminateProcess(info.hProcess, ERROR_PROCESS_ABORTED))
                    result.terminated = true;
                WaitForSingleObject(info.hProcess, INFINITE);
            }

            DWORD exit_code = 0;
            if (!GetExitCodeProcess(info.hProcess, &exit_code))
            {
                if (!result.final_error)
                    result.final_error = error_code(GetLastError());
            }
            else
            {
                result.exit_code = static_cast<int>(exit_code);
            }

            writer.join();
            stdout_reader.join();
            stderr_reader.join();

            const DWORD io = io_error.load(std::memory_order_relaxed);
            if (io != ERROR_SUCCESS && !result.final_error)
                result.final_error = error_code(io);
            return result;
        }
        catch (const std::system_error& error)
        {
            result.final_error = error.code();
            return result;
        }
    }
}
