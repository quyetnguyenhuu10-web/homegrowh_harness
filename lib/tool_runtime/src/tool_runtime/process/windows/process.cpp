#include "../process.h"

#include "../../error/error.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string_view>
#include <thread>

namespace tool_runtime::detail::process
{
    namespace
    {
        Error win32_error(
            std::string_view operation,
            std::string_view api,
            DWORD code,
            nlohmann::json&& details = nlohmann::json::object())
        {
            Error error = make_system_error(
                operation, api, {static_cast<int>(code), std::system_category()}, std::move(details));
            error.data.front()["code"] = code;
            return error;
        }

        struct handle_closer
        {
            DWORD* error_code = nullptr;

            void operator()(void* value) const noexcept
            {
                if (value != nullptr && value != INVALID_HANDLE_VALUE)
                {
                    if (!CloseHandle(static_cast<HANDLE>(value)))
                    {
                        const DWORD code = GetLastError();
                        if (error_code != nullptr)
                            *error_code = code;
                    }
                }
            }
        };

        using unique_handle = std::unique_ptr<void, handle_closer>;

        struct pipe_pair
        {
            unique_handle read{nullptr, handle_closer{}};
            unique_handle write{nullptr, handle_closer{}};
        };

        std::optional<Error> make_pipe(
            pipe_pair& pipe,
            bool parent_reads,
            std::string_view stream,
            DWORD& read_close_error,
            DWORD& write_close_error)
        {
            SECURITY_ATTRIBUTES attributes{};
            attributes.nLength = sizeof(attributes);
            attributes.bInheritHandle = TRUE;
            HANDLE read = nullptr;
            HANDLE write = nullptr;
            if (!CreatePipe(&read, &write, &attributes, 0))
            {
                const DWORD code = GetLastError();
                return win32_error("create_pipe", "CreatePipe", code, {{"stream", stream}});
            }
            pipe.read = unique_handle(read, handle_closer{&read_close_error});
            pipe.write = unique_handle(write, handle_closer{&write_close_error});
            HANDLE parent = parent_reads
                ? static_cast<HANDLE>(pipe.read.get())
                : static_cast<HANDLE>(pipe.write.get());
            if (!SetHandleInformation(parent, HANDLE_FLAG_INHERIT, 0))
            {
                const DWORD code = GetLastError();
                return win32_error("configure_pipe", "SetHandleInformation", code, {{"stream", stream}});
            }
            return std::nullopt;
        }

        Result<std::wstring> utf8_to_wide(const std::string& value)
        {
            if (value.empty())
                return Result<std::wstring>::success(std::wstring{});
            if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
                return Result<std::wstring>::failure(make_error(
                    "encode_argument", "validation_error", "Argument exceeds the conversion API size limit",
                    {{"api", "MultiByteToWideChar"}, {"size", value.size()}}));
            const int size = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
            if (size <= 0)
            {
                const DWORD code = GetLastError();
                return Result<std::wstring>::failure(win32_error(
                    "encode_argument", "MultiByteToWideChar", code));
            }
            std::wstring output(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(), size) != size)
            {
                const DWORD code = GetLastError();
                return Result<std::wstring>::failure(win32_error(
                    "encode_argument", "MultiByteToWideChar", code));
            }
            return Result<std::wstring>::success(std::move(output));
        }

        std::wstring quote(std::wstring_view argument)
        {
            if (!argument.empty() && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
                return std::wstring(argument);
            std::wstring output(1, L'"');
            std::size_t backslashes = 0;
            for (wchar_t value : argument)
            {
                if (value == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (value == L'"')
                {
                    output.append(backslashes * 2 + 1, L'\\');
                    output.push_back(L'"');
                    backslashes = 0;
                    continue;
                }
                output.append(backslashes, L'\\');
                backslashes = 0;
                output.push_back(value);
            }
            output.append(backslashes * 2, L'\\');
            output.push_back(L'"');
            return output;
        }

        Result<std::wstring> command_line(
            const std::filesystem::path& executable,
            const std::vector<std::string>& arguments)
        {
            std::wstring output = quote(executable.wstring());
            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                auto argument = utf8_to_wide(arguments[index]);
                if (argument.error)
                {
                    argument.error->data.push_back({{"argument_index", index}, {"argument", text_payload(arguments[index])}});
                    return Result<std::wstring>::failure(std::move(*argument.error));
                }
                output.push_back(L' ');
                output += quote(*argument.value);
            }
            return Result<std::wstring>::success(std::move(output));
        }

        void read_all(unique_handle handle, std::string& output, std::optional<Error>& error, std::string_view stream)
        {
            try
            {
                std::array<char, 8192> buffer{};
                for (;;)
                {
                    DWORD read = 0;
                    if (!ReadFile(static_cast<HANDLE>(handle.get()), buffer.data(),
                            static_cast<DWORD>(buffer.size()), &read, nullptr))
                    {
                        const DWORD code = GetLastError();
                        if (code != ERROR_BROKEN_PIPE)
                            error = win32_error("read_process_output", "ReadFile", code,
                                {{"stream", stream}, {"bytes_read", output.size()}});
                        return;
                    }
                    if (read == 0)
                        return;
                    output.append(buffer.data(), read);
                }
            }
            catch (...)
            {
                error = current_exception_error("read_process_output");
            }
        }

        void write_all(unique_handle handle, const std::string& input, std::optional<Error>& error)
        {
            try
            {
                std::size_t offset = 0;
                while (offset < input.size())
                {
                    const DWORD remaining = static_cast<DWORD>((std::min)(
                        input.size() - offset, static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
                    DWORD written = 0;
                    if (!WriteFile(static_cast<HANDLE>(handle.get()), input.data() + offset,
                            remaining, &written, nullptr))
                    {
                        const DWORD code = GetLastError();
                        error = win32_error("write_process_input", "WriteFile", code, {{"bytes_written", offset}});
                        return;
                    }
                    if (written == 0)
                    {
                        error = make_error("write_process_input", "io_error", "WriteFile made no progress",
                            {{"api", "WriteFile"}, {"bytes_written", offset}, {"bytes_remaining", remaining}});
                        return;
                    }
                    offset += written;
                }
            }
            catch (...)
            {
                error = current_exception_error("write_process_input");
            }
        }

        struct process_guard
        {
            HANDLE handle;
            DWORD& termination_error;
            bool active = true;

            ~process_guard()
            {
                if (active && !TerminateProcess(handle, EXIT_FAILURE))
                    termination_error = GetLastError();
            }
        };
    }

    result run_platform(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data)
    {
        result output;
        std::array<DWORD, 8> close_errors{};
        DWORD termination_error = 0;
        std::array<std::optional<Error>, 3> stream_errors;
        const auto execute = [&]
        {
            try
            {
                pipe_pair stdin_pipe;
                pipe_pair stdout_pipe;
                pipe_pair stderr_pipe;
                if (auto error = make_pipe(stdin_pipe, false, "stdin", close_errors[0], close_errors[1]))
                {
                    output.error = std::move(error);
                    return;
                }
                if (auto error = make_pipe(stdout_pipe, true, "stdout", close_errors[2], close_errors[3]))
                {
                    output.error = std::move(error);
                    return;
                }
                if (auto error = make_pipe(stderr_pipe, true, "stderr", close_errors[4], close_errors[5]))
                {
                    output.error = std::move(error);
                    return;
                }
                auto command = command_line(executable, arguments);
                if (command.error)
                {
                    output.error = std::move(command.error);
                    return;
                }
                STARTUPINFOW startup{};
                startup.cb = sizeof(startup);
                startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
                startup.wShowWindow = SW_HIDE;
                startup.hStdInput = static_cast<HANDLE>(stdin_pipe.read.get());
                startup.hStdOutput = static_cast<HANDLE>(stdout_pipe.write.get());
                startup.hStdError = static_cast<HANDLE>(stderr_pipe.write.get());
                PROCESS_INFORMATION information{};
                const std::wstring executable_wide = executable.wstring();
                const std::wstring directory_wide = working_directory.wstring();
                if (!CreateProcessW(executable_wide.c_str(), command.value->data(), nullptr, nullptr,
                        TRUE, CREATE_NO_WINDOW, nullptr,
                        working_directory.empty() ? nullptr : directory_wide.c_str(), &startup, &information))
                {
                    const DWORD code = GetLastError();
                    output.error = win32_error("start_process", "CreateProcessW", code);
                    return;
                }
                unique_handle process(information.hProcess, handle_closer{&close_errors[6]});
                unique_handle thread(information.hThread, handle_closer{&close_errors[7]});
                output.started = true;
                stdin_pipe.read.reset();
                stdout_pipe.write.reset();
                stderr_pipe.write.reset();
                std::jthread stdin_writer;
                std::jthread stdout_reader;
                std::jthread stderr_reader;
                // Kill a partially supervised child before the joining threads unwind.
                process_guard guard{static_cast<HANDLE>(process.get()), termination_error};
                stdin_writer = std::jthread(write_all, std::move(stdin_pipe.write), std::cref(stdin_data), std::ref(stream_errors[0]));
                stdout_reader = std::jthread(read_all, std::move(stdout_pipe.read), std::ref(output.stdout_text), std::ref(stream_errors[1]), "stdout");
                stderr_reader = std::jthread(read_all, std::move(stderr_pipe.read), std::ref(output.stderr_text), std::ref(stream_errors[2]), "stderr");
                const DWORD wait = WaitForSingleObject(static_cast<HANDLE>(process.get()), INFINITE);
                if (wait == WAIT_FAILED)
                {
                    const DWORD code = GetLastError();
                    output.error = win32_error("wait_process", "WaitForSingleObject", code);
                    return;
                }
                if (wait != WAIT_OBJECT_0)
                {
                    output.error = make_error("wait_process", "protocol_error", "Unexpected process wait result",
                        {{"api", "WaitForSingleObject"}, {"wait_result", wait}});
                    return;
                }
                guard.active = false;
                DWORD exit_code = 0;
                if (!GetExitCodeProcess(static_cast<HANDLE>(process.get()), &exit_code))
                {
                    const DWORD code = GetLastError();
                    output.error = win32_error("get_process_exit_code", "GetExitCodeProcess", code);
                }
                else
                    output.exit_code = exit_code;
            }
            catch (...)
            {
                append_error(output.error, current_exception_error("run_process"));
            }
        };
        execute();
        for (auto& error : stream_errors)
        {
            if (error)
                append_error(output.error, std::move(*error));
        }
        for (std::size_t index = 0; index < close_errors.size(); ++index)
        {
            if (close_errors[index] != 0)
                append_error(output.error, win32_error("close_process_handle", "CloseHandle", close_errors[index], {{"resource_index", index}}));
        }
        if (termination_error != 0)
            append_error(output.error, win32_error("terminate_process", "TerminateProcess", termination_error));
        return output;
    }
}
