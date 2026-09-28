#include "process.h"

#include <array>
#include <thread>

#if defined(_WIN32)

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <string_view>

namespace tool_runtime::detail::process
{
    namespace
    {
        struct handle_closer
        {
            void operator()(void* value) const noexcept
            {
                if (value != nullptr && value != INVALID_HANDLE_VALUE)
                    CloseHandle(static_cast<HANDLE>(value));
            }
        };

        using unique_handle = std::unique_ptr<void, handle_closer>;

        unique_handle own(HANDLE value)
        {
            return unique_handle(value);
        }

        struct pipe_pair
        {
            unique_handle read;
            unique_handle write;
        };

        pipe_pair make_pipe(bool parent_reads)
        {
            SECURITY_ATTRIBUTES attributes{};
            attributes.nLength = sizeof(attributes);
            attributes.bInheritHandle = TRUE;

            HANDLE read = nullptr;
            HANDLE write = nullptr;
            if (!CreatePipe(&read, &write, &attributes, 0))
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "CreatePipe(tool_runtime)");

            pipe_pair pipe{own(read), own(write)};
            HANDLE parent = parent_reads
                ? static_cast<HANDLE>(pipe.read.get())
                : static_cast<HANDLE>(pipe.write.get());
            if (!SetHandleInformation(parent, HANDLE_FLAG_INHERIT, 0))
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "SetHandleInformation(tool_runtime)");
            return pipe;
        }

        std::wstring utf8_to_wide(const std::string& value)
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
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "MultiByteToWideChar(tool_runtime)");
            std::wstring output(static_cast<std::size_t>(size), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    output.data(),
                    size) != size)
            {
                throw std::system_error(
                    static_cast<int>(GetLastError()),
                    std::system_category(),
                    "MultiByteToWideChar(tool_runtime)");
            }
            return output;
        }

        std::wstring quote(std::wstring_view argument)
        {
            if (!argument.empty()
                && argument.find_first_of(L" \t\n\v\"") == std::wstring_view::npos)
            {
                return std::wstring(argument);
            }

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

        std::wstring command_line(
            const std::filesystem::path& executable,
            const std::vector<std::string>& arguments)
        {
            std::wstring output = quote(executable.wstring());
            for (const std::string& argument : arguments)
            {
                output.push_back(L' ');
                output += quote(utf8_to_wide(argument));
            }
            return output;
        }

        void read_all(unique_handle handle, std::string& output)
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
                    if (GetLastError() == ERROR_BROKEN_PIPE)
                        return;
                    return;
                }
                if (read == 0)
                    return;
                output.append(buffer.data(), read);
            }
        }

        void write_all(unique_handle handle, const std::string& input)
        {
            std::size_t offset = 0;
            while (offset < input.size())
            {
                const DWORD remaining = static_cast<DWORD>((std::min)(
                    input.size() - offset,
                    static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())));
                DWORD written = 0;
                if (!WriteFile(
                        static_cast<HANDLE>(handle.get()),
                        input.data() + offset,
                        remaining,
                        &written,
                        nullptr)
                    || written == 0)
                {
                    return;
                }
                offset += written;
            }
        }
    }

    result run(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data)
    {
        result output;
        try
        {
            pipe_pair stdin_pipe = make_pipe(false);
            pipe_pair stdout_pipe = make_pipe(true);
            pipe_pair stderr_pipe = make_pipe(true);

            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            startup.hStdInput = static_cast<HANDLE>(stdin_pipe.read.get());
            startup.hStdOutput = static_cast<HANDLE>(stdout_pipe.write.get());
            startup.hStdError = static_cast<HANDLE>(stderr_pipe.write.get());

            std::wstring command = command_line(executable, arguments);
            std::vector<wchar_t> command_buffer(command.begin(), command.end());
            command_buffer.push_back(L'\0');

            PROCESS_INFORMATION information{};
            const std::wstring executable_wide = executable.wstring();
            const std::wstring working_directory_wide =
                working_directory.wstring();

            if (!CreateProcessW(
                    executable_wide.c_str(),
                    command_buffer.data(),
                    nullptr,
                    nullptr,
                    TRUE,
                    CREATE_NO_WINDOW,
                    nullptr,
                    working_directory.empty()
                        ? nullptr
                        : working_directory_wide.c_str(),
                    &startup,
                    &information))
            {
                output.error = std::error_code(
                    static_cast<int>(GetLastError()),
                    std::system_category());
                return output;
            }

            unique_handle process = own(information.hProcess);
            unique_handle thread = own(information.hThread);
            output.started = true;

            stdin_pipe.read.reset();
            stdout_pipe.write.reset();
            stderr_pipe.write.reset();

            std::thread stdin_writer(
                write_all,
                std::move(stdin_pipe.write),
                std::cref(stdin_data));
            std::thread stdout_reader(
                read_all,
                std::move(stdout_pipe.read),
                std::ref(output.stdout_text));
            std::thread stderr_reader(
                read_all,
                std::move(stderr_pipe.read),
                std::ref(output.stderr_text));

            WaitForSingleObject(static_cast<HANDLE>(process.get()), INFINITE);
            DWORD exit_code = 0;
            if (GetExitCodeProcess(
                    static_cast<HANDLE>(process.get()),
                    &exit_code))
            {
                output.exit_code = static_cast<int>(exit_code);
            }

            stdin_writer.join();
            stdout_reader.join();
            stderr_reader.join();
            return output;
        }
        catch (const std::system_error& error)
        {
            output.error = error.code();
            return output;
        }
    }
}

#elif defined(__linux__)

#include <cerrno>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace tool_runtime::detail::process
{
    namespace
    {
        struct fd_pair
        {
            int read = -1;
            int write = -1;
        };

        fd_pair make_pipe()
        {
            int values[2]{-1, -1};
            if (::pipe(values) != 0)
                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "pipe(tool_runtime)");
            return {values[0], values[1]};
        }

        void close_fd(int& fd) noexcept
        {
            if (fd >= 0)
            {
                ::close(fd);
                fd = -1;
            }
        }

        void read_all(int fd, std::string& output)
        {
            std::array<char, 8192> buffer{};
            for (;;)
            {
                const ssize_t bytes = ::read(fd, buffer.data(), buffer.size());
                if (bytes > 0)
                {
                    output.append(buffer.data(), static_cast<std::size_t>(bytes));
                    continue;
                }
                if (bytes < 0 && errno == EINTR)
                    continue;
                break;
            }
            ::close(fd);
        }

        void write_all(int fd, const std::string& input)
        {
            std::size_t offset = 0;
            while (offset < input.size())
            {
                const ssize_t bytes = ::write(
                    fd,
                    input.data() + offset,
                    input.size() - offset);
                if (bytes > 0)
                {
                    offset += static_cast<std::size_t>(bytes);
                    continue;
                }
                if (bytes < 0 && errno == EINTR)
                    continue;
                break;
            }
            ::close(fd);
        }
    }

    result run(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data)
    {
        result output;
        try
        {
            fd_pair stdin_pipe = make_pipe();
            fd_pair stdout_pipe = make_pipe();
            fd_pair stderr_pipe = make_pipe();

            const pid_t pid = fork();
            if (pid < 0)
            {
                output.error = std::error_code(errno, std::generic_category());
                return output;
            }

            if (pid == 0)
            {
                if (dup2(stdin_pipe.read, STDIN_FILENO) < 0
                    || dup2(stdout_pipe.write, STDOUT_FILENO) < 0
                    || dup2(stderr_pipe.write, STDERR_FILENO) < 0)
                {
                    _exit(126);
                }

                close_fd(stdin_pipe.read);
                close_fd(stdin_pipe.write);
                close_fd(stdout_pipe.read);
                close_fd(stdout_pipe.write);
                close_fd(stderr_pipe.read);
                close_fd(stderr_pipe.write);

                if (!working_directory.empty()
                    && chdir(working_directory.c_str()) != 0)
                {
                    _exit(126);
                }

                std::vector<std::string> storage;
                storage.reserve(arguments.size() + 1);
                storage.push_back(executable.string());
                storage.insert(storage.end(), arguments.begin(), arguments.end());

                std::vector<char*> argv;
                argv.reserve(storage.size() + 1);
                for (std::string& argument : storage)
                    argv.push_back(argument.data());
                argv.push_back(nullptr);

                execv(storage.front().c_str(), argv.data());
                _exit(127);
            }

            output.started = true;
            close_fd(stdin_pipe.read);
            close_fd(stdout_pipe.write);
            close_fd(stderr_pipe.write);

            std::thread stdin_writer(
                write_all,
                stdin_pipe.write,
                std::cref(stdin_data));
            stdin_pipe.write = -1;
            std::thread stdout_reader(
                read_all,
                stdout_pipe.read,
                std::ref(output.stdout_text));
            stdout_pipe.read = -1;
            std::thread stderr_reader(
                read_all,
                stderr_pipe.read,
                std::ref(output.stderr_text));
            stderr_pipe.read = -1;

            int status = 0;
            while (waitpid(pid, &status, 0) < 0)
            {
                if (errno == EINTR)
                    continue;
                output.error = std::error_code(errno, std::generic_category());
                break;
            }

            if (WIFEXITED(status))
                output.exit_code = WEXITSTATUS(status);
            else if (WIFSIGNALED(status))
                output.exit_code = 128 + WTERMSIG(status);

            stdin_writer.join();
            stdout_reader.join();
            stderr_reader.join();
            return output;
        }
        catch (const std::system_error& error)
        {
            output.error = error.code();
            return output;
        }
    }
}

#else
#error "Unsupported operating system"
#endif
