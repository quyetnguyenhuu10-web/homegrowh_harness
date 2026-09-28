#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "process.h"

#include "../apply_config.h"
#include "appcontainer.h"
#include "job.h"
#include "raii.h"

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace sandbox::detail::process::windows
{
    namespace
    {
        struct pipe_pair
        {
            unique_handle read;
            unique_handle write;
        };

        struct attribute_list_deleter
        {
            void operator()(void* value) const noexcept
            {
                if (value == nullptr)
                    return;
                auto* attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(value);
                DeleteProcThreadAttributeList(attributes);
                HeapFree(GetProcessHeap(), 0, value);
            }
        };

        using unique_attribute_list =
            std::unique_ptr<void, attribute_list_deleter>;

        struct heap_free_deleter
        {
            void operator()(void* value) const noexcept
            {
                if (value != nullptr)
                    HeapFree(GetProcessHeap(), 0, value);
            }
        };

        using unique_heap_memory = std::unique_ptr<void, heap_free_deleter>;

        class joining_thread final
        {
        public:
            template <typename Function, typename... Arguments>
            explicit joining_thread(Function&& function, Arguments&&... arguments)
                : thread_(
                    std::forward<Function>(function),
                    std::forward<Arguments>(arguments)...)
            {
            }

            joining_thread(const joining_thread&) = delete;
            joining_thread& operator=(const joining_thread&) = delete;

            ~joining_thread()
            {
                if (thread_.joinable())
                    thread_.join();
            }

            void join()
            {
                if (thread_.joinable())
                    thread_.join();
            }

        private:
            std::thread thread_;
        };

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

        [[noreturn]] void throw_win32(const char* action, DWORD error)
        {
            throw std::system_error(
                static_cast<int>(error),
                std::system_category(),
                action);
        }

        void remember_io_error(
            std::atomic<DWORD>& destination,
            DWORD error) noexcept
        {
            if (error == ERROR_SUCCESS || error == ERROR_BROKEN_PIPE)
                return;
            destination.store(error, std::memory_order_relaxed);
        }

        pipe_pair create_pipe(bool parent_reads)
        {
            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;

            HANDLE raw_read = nullptr;
            HANDLE raw_write = nullptr;
            if (!CreatePipe(&raw_read, &raw_write, &security, 0))
                throw_win32("CreatePipe(sandbox process)", GetLastError());

            pipe_pair pipe{
                own_handle(raw_read),
                own_handle(raw_write),
            };

            HANDLE parent_handle = parent_reads
                ? static_cast<HANDLE>(pipe.read.get())
                : static_cast<HANDLE>(pipe.write.get());
            if (!SetHandleInformation(parent_handle, HANDLE_FLAG_INHERIT, 0))
                throw_win32("SetHandleInformation(sandbox process)", GetLastError());
            return pipe;
        }

        std::wstring quote_argument(std::wstring_view argument)
        {
            if (!argument.empty()
                && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
            {
                return std::wstring(argument);
            }

            std::wstring output;
            output.push_back(L'\"');
            std::size_t backslashes = 0;
            for (const wchar_t value : argument)
            {
                if (value == L'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (value == L'\"')
                {
                    output.append(backslashes * 2 + 1, L'\\');
                    output.push_back(L'\"');
                    backslashes = 0;
                    continue;
                }
                output.append(backslashes, L'\\');
                backslashes = 0;
                output.push_back(value);
            }
            output.append(backslashes * 2, L'\\');
            output.push_back(L'\"');
            return output;
        }

        std::wstring command_line(const process_request& request)
        {
            return quote_argument(request.executable.wstring());
        }

        unique_attribute_list create_attribute_list(
            const appcontainer_security& security,
            const std::array<HANDLE, 3>& inherited_handles)
        {
            SIZE_T bytes = 0;
            InitializeProcThreadAttributeList(nullptr, 2, 0, &bytes);
            if (bytes == 0)
            {
                throw_win32(
                    "InitializeProcThreadAttributeList(size)",
                    GetLastError());
            }

            unique_heap_memory storage(
                HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes));
            if (!storage)
                throw_win32("HeapAlloc(process attributes)", ERROR_NOT_ENOUGH_MEMORY);

            auto* list = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.get());
            if (!InitializeProcThreadAttributeList(list, 2, 0, &bytes))
            {
                throw_win32(
                    "InitializeProcThreadAttributeList",
                    GetLastError());
            }

            unique_attribute_list attributes(storage.release());

            security.apply(list);
            if (!UpdateProcThreadAttribute(
                    list,
                    0,
                    PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                    const_cast<HANDLE*>(inherited_handles.data()),
                    sizeof(HANDLE) * inherited_handles.size(),
                    nullptr,
                    nullptr))
            {
                throw_win32(
                    "UpdateProcThreadAttribute(HANDLE_LIST)",
                    GetLastError());
            }
            return attributes;
        }

        void read_pipe(
            unique_handle handle,
            std::string& output,
            std::atomic<DWORD>& io_error) noexcept
        {
            std::array<char, 8192> buffer{};
            for (;;)
            {
                DWORD read = 0;
                if (!ReadFile(
                        static_cast<HANDLE>(handle.get()),
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &read,
                        nullptr))
                {
                    const DWORD error = GetLastError();
                    remember_io_error(io_error, error);
                    return;
                }
                if (read == 0)
                    return;
                output.append(buffer.data(), read);
            }
        }

        void write_pipe(
            unique_handle handle,
            const std::string& input,
            std::atomic<DWORD>& io_error) noexcept
        {
            std::size_t offset = 0;
            while (offset < input.size())
            {
                DWORD written = 0;
                const DWORD remaining = static_cast<DWORD>(
                    (std::min)(
                        input.size() - offset,
                        static_cast<std::size_t>(
                            (std::numeric_limits<DWORD>::max)())));
                if (!WriteFile(
                        static_cast<HANDLE>(handle.get()),
                        input.data() + offset,
                        remaining,
                        &written,
                        nullptr))
                {
                    const DWORD error = GetLastError();
                    remember_io_error(io_error, error);
                    return;
                }
                if (written == 0)
                {
                    remember_io_error(io_error, ERROR_WRITE_FAULT);
                    return;
                }
                offset += written;
            }
        }

        DWORD timeout_value(const std::chrono::milliseconds timeout)
        {
            if (timeout.count() <= 0)
                return 1;
            return static_cast<DWORD>((std::min)(
                timeout.count(),
                static_cast<long long>(INFINITE - 1)));
        }
    }

    process_results run(const process_request& request)
    {
        process_results result;
        try
        {
            applied_config applied = apply_config(request.config, request.refresh);
            result.state.config = std::move(applied.results);
            if (result.state.config.final_error
                || applied.filesystem.permissions.size()
                    != applied.filesystem_count)
            {
                return result;
            }

            appcontainer_security security(
                applied.filesystem,
                applied.network);
            unique_handle job = create_process_job();

            pipe_pair stdin_pipe = create_pipe(false);
            pipe_pair stdout_pipe = create_pipe(true);
            pipe_pair stderr_pipe = create_pipe(true);

            const std::array<HANDLE, 3> inherited_handles{
                static_cast<HANDLE>(stdin_pipe.read.get()),
                static_cast<HANDLE>(stdout_pipe.write.get()),
                static_cast<HANDLE>(stderr_pipe.write.get()),
            };
            unique_attribute_list attributes = create_attribute_list(
                security,
                inherited_handles);

            STARTUPINFOEXW startup{};
            startup.StartupInfo.cb = sizeof(startup);
            startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            startup.StartupInfo.wShowWindow = SW_HIDE;
            startup.StartupInfo.hStdInput = inherited_handles[0];
            startup.StartupInfo.hStdOutput = inherited_handles[1];
            startup.StartupInfo.hStdError = inherited_handles[2];
            startup.lpAttributeList = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
                attributes.get());

            std::wstring mutable_command = command_line(request);
            std::vector<wchar_t> command_buffer(
                mutable_command.begin(),
                mutable_command.end());
            command_buffer.push_back(L'\0');

            PROCESS_INFORMATION information{};
            const std::wstring executable = request.executable.wstring();
            const std::wstring working_directory = request.working_directory.wstring();
            const DWORD creation_flags =
                CREATE_SUSPENDED
                | CREATE_NO_WINDOW
                | EXTENDED_STARTUPINFO_PRESENT;

            if (!CreateProcessW(
                    executable.c_str(),
                    command_buffer.data(),
                    nullptr,
                    nullptr,
                    TRUE,
                    creation_flags,
                    nullptr,
                    working_directory.empty() ? nullptr : working_directory.c_str(),
                    &startup.StartupInfo,
                    &information))
            {
                throw_win32("CreateProcessW(sandbox target)", GetLastError());
            }

            unique_handle process = own_handle(information.hProcess);
            unique_handle thread = own_handle(information.hThread);
            suspended_process_guard suspended_guard(
                static_cast<HANDLE>(process.get()));
            assign_process_to_job(
                static_cast<HANDLE>(job.get()),
                static_cast<HANDLE>(process.get()));

            stdin_pipe.read.reset();
            stdout_pipe.write.reset();
            stderr_pipe.write.reset();

            if (ResumeThread(static_cast<HANDLE>(thread.get())) == static_cast<DWORD>(-1))
            {
                const DWORD error = GetLastError();
                TerminateJobObject(static_cast<HANDLE>(job.get()), error);
                throw_win32("ResumeThread(sandbox target)", error);
            }
            suspended_guard.release();
            result.state.started = true;

            std::atomic<DWORD> io_error{ERROR_SUCCESS};
            joining_thread stdin_writer(
                write_pipe,
                std::move(stdin_pipe.write),
                std::cref(request.stdin_data),
                std::ref(io_error));
            joining_thread stdout_reader(
                read_pipe,
                std::move(stdout_pipe.read),
                std::ref(result.stdout_text),
                std::ref(io_error));
            joining_thread stderr_reader(
                read_pipe,
                std::move(stderr_pipe.read),
                std::ref(result.stderr_text),
                std::ref(io_error));

            SetLastError(ERROR_SUCCESS);
            const DWORD wait = WaitForSingleObject(
                static_cast<HANDLE>(process.get()),
                timeout_value(request.timeout));

            if (wait == WAIT_TIMEOUT)
            {
                DWORD captured = GetLastError();
                if (captured == ERROR_SUCCESS)
                    captured = io_error.load(std::memory_order_relaxed);

                result.state.timed_out = true;
                result.state.os_error_before_termination = std::error_code(
                    static_cast<int>(captured),
                    std::system_category());

                if (!TerminateJobObject(
                        static_cast<HANDLE>(job.get()),
                        ERROR_TIMEOUT))
                {
                    result.state.final_error = std::error_code(
                        static_cast<int>(GetLastError()),
                        std::system_category());
                }
                result.state.terminated = true;
                WaitForSingleObject(static_cast<HANDLE>(process.get()), INFINITE);
            }
            else if (wait == WAIT_FAILED)
            {
                const DWORD error = GetLastError();
                result.state.final_error = std::error_code(
                    static_cast<int>(error),
                    std::system_category());
                TerminateJobObject(static_cast<HANDLE>(job.get()), error);
                result.state.terminated = true;
                WaitForSingleObject(static_cast<HANDLE>(process.get()), INFINITE);
            }

            DWORD exit_code = 0;
            if (GetExitCodeProcess(static_cast<HANDLE>(process.get()), &exit_code))
                result.state.exit_code = static_cast<int>(exit_code);
            else if (!result.state.final_error)
            {
                result.state.final_error = std::error_code(
                    static_cast<int>(GetLastError()),
                    std::system_category());
            }

            stdin_writer.join();
            stdout_reader.join();
            stderr_reader.join();
            return result;
        }
        catch (const std::system_error& exception)
        {
            result.state.final_error = exception.code();
            return result;
        }
    }
}
