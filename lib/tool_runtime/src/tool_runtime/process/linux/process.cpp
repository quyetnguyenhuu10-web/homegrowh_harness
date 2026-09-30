#include "../process.h"

#include "../../error/error.h"
#include "../../platform/platform.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace tool_runtime::detail::process
{
    namespace
    {
        class unique_fd
        {
        public:
            unique_fd() = default;
            unique_fd(int value, int& close_error) : value_(value), close_error_(&close_error) {}
            unique_fd(const unique_fd&) = delete;
            unique_fd& operator=(const unique_fd&) = delete;
            unique_fd(unique_fd&& other) noexcept
                : value_(std::exchange(other.value_, -1)), close_error_(other.close_error_) {}
            unique_fd& operator=(unique_fd&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    value_ = std::exchange(other.value_, -1);
                    close_error_ = other.close_error_;
                }
                return *this;
            }
            ~unique_fd() { reset(); }
            int get() const { return value_; }
            void reset() noexcept
            {
                const int value = std::exchange(value_, -1);
                // close must not be retried after EINTR on Linux: the descriptor is released.
                if (value >= 0 && ::close(value) != 0)
                {
                    const int code = errno;
                    if (close_error_ != nullptr)
                        *close_error_ = code;
                }
            }

        private:
            int value_ = -1;
            int* close_error_ = nullptr;
        };

        struct fd_pair
        {
            unique_fd read;
            unique_fd write;
        };

        std::optional<Error> make_pipe(fd_pair& target, int& read_close_error, int& write_close_error)
        {
            int values[2]{-1, -1};
            if (::pipe2(values, O_CLOEXEC) != 0)
            {
                const int code = errno;
                return make_system_error("create_pipe", "pipe2", {code, std::generic_category()});
            }
            target.read = unique_fd(values[0], read_close_error);
            target.write = unique_fd(values[1], write_close_error);
            for (unique_fd* descriptor : {&target.read, &target.write})
            {
                if (descriptor->get() > STDERR_FILENO)
                    continue;
                const int moved = ::fcntl(descriptor->get(), F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
                if (moved < 0)
                {
                    const int code = errno;
                    return make_system_error("configure_pipe", "fcntl", {code, std::generic_category()});
                }
                int& close_error = descriptor == &target.read ? read_close_error : write_close_error;
                *descriptor = unique_fd(moved, close_error);
            }
            return std::nullopt;
        }

        enum class child_operation { duplicate_stdin, duplicate_stdout, duplicate_stderr, close_descriptor, change_directory, execute };
        struct child_failure
        {
            child_operation operation;
            int code;
            int descriptor;
        };

        [[noreturn]] void report_child_failure(int descriptor, child_operation operation, int code, int failed_descriptor = -1)
        {
            const child_failure failure{operation, code, failed_descriptor};
            const char* bytes = reinterpret_cast<const char*>(&failure);
            std::size_t offset = 0;
            while (offset < sizeof(failure))
            {
                const ssize_t count = ::write(descriptor, bytes + offset, sizeof(failure) - offset);
                if (count > 0)
                    offset += static_cast<std::size_t>(count);
                else if (count < 0 && errno == EINTR)
                    continue;
                else
                    break;
            }
            ::_exit(operation == child_operation::execute ? 127 : 126);
        }

        struct process_guard
        {
            pid_t pid;
            int& kill_error;
            int& wait_error;
            bool active = true;
            ~process_guard()
            {
                if (!active)
                    return;
                if (::kill(pid, SIGKILL) != 0)
                    kill_error = errno;
                int status = 0;
                while (::waitpid(pid, &status, 0) < 0)
                {
                    const int code = errno;
                    if (code == EINTR)
                        continue;
                    wait_error = code;
                    break;
                }
            }
        };

        void read_all(unique_fd fd, std::string& output, std::optional<Error>& error, std::string_view stream)
        {
            try
            {
                std::array<char, 8192> buffer{};
                for (;;)
                {
                    const ssize_t bytes = ::read(fd.get(), buffer.data(), buffer.size());
                    if (bytes > 0)
                    {
                        output.append(buffer.data(), static_cast<std::size_t>(bytes));
                        continue;
                    }
                    if (bytes == 0)
                        return;
                    const int code = errno;
                    if (code == EINTR)
                        continue;
                    error = make_system_error("read_process_output", "read", {code, std::generic_category()},
                        {{"stream", stream}, {"bytes_read", output.size()}});
                    return;
                }
            }
            catch (...)
            {
                error = current_exception_error("read_process_output");
            }
        }

        struct sigpipe_guard
        {
            sigset_t blocked{};
            sigset_t previous{};
            int& drain_error;
            int& restore_error;
            bool active = false;
            bool pending_before = false;
            bool broken_pipe = false;

            std::optional<Error> block()
            {
                if (::sigemptyset(&blocked) != 0)
                {
                    const int code = errno;
                    return make_system_error("block_sigpipe", "sigemptyset", {code, std::generic_category()});
                }
                if (::sigaddset(&blocked, SIGPIPE) != 0)
                {
                    const int code = errno;
                    return make_system_error("block_sigpipe", "sigaddset", {code, std::generic_category()});
                }
                const int code = ::pthread_sigmask(SIG_BLOCK, &blocked, &previous);
                if (code != 0)
                    return make_system_error("block_sigpipe", "pthread_sigmask", {code, std::generic_category()});
                active = true;
                sigset_t pending{};
                if (::sigpending(&pending) != 0)
                {
                    const int pending_code = errno;
                    return make_system_error("inspect_sigpipe", "sigpending", {pending_code, std::generic_category()});
                }
                const int member = ::sigismember(&pending, SIGPIPE);
                if (member < 0)
                {
                    const int member_code = errno;
                    return make_system_error("inspect_sigpipe", "sigismember", {member_code, std::generic_category()});
                }
                pending_before = member == 1;
                return std::nullopt;
            }

            ~sigpipe_guard()
            {
                if (!active)
                    return;
                if (broken_pipe && !pending_before)
                {
                    const timespec timeout{};
                    while (::sigtimedwait(&blocked, nullptr, &timeout) < 0)
                    {
                        const int code = errno;
                        if (code == EINTR)
                            continue;
                        if (code != EAGAIN)
                            drain_error = code;
                        break;
                    }
                }
                const int code = ::pthread_sigmask(SIG_SETMASK, &previous, nullptr);
                if (code != 0)
                    restore_error = code;
            }
        };

        void write_all(unique_fd fd, const std::string& input, std::optional<Error>& error, std::array<int, 2>& signal_errors)
        {
            try
            {
                sigpipe_guard signal{ {}, {}, signal_errors[0], signal_errors[1] };
                if (auto failure = signal.block())
                {
                    error = std::move(failure);
                    return;
                }
                std::size_t offset = 0;
                while (offset < input.size())
                {
                    const ssize_t bytes = ::write(fd.get(), input.data() + offset, input.size() - offset);
                    if (bytes > 0)
                    {
                        offset += static_cast<std::size_t>(bytes);
                        continue;
                    }
                    if (bytes == 0)
                    {
                        error = make_error("write_process_input", "io_error", "write made no progress",
                            {{"api", "write"}, {"bytes_written", offset}});
                        return;
                    }
                    const int code = errno;
                    if (code == EINTR)
                        continue;
                    signal.broken_pipe = code == EPIPE;
                    error = make_system_error("write_process_input", "write", {code, std::generic_category()},
                        {{"bytes_written", offset}});
                    return;
                }
            }
            catch (...)
            {
                error = current_exception_error("write_process_input");
            }
        }
    }

    result run_platform(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data)
    {
        result output;
        std::array<int, 8> close_errors{};
        std::array<int, 2> signal_errors{};
        int kill_error = 0;
        int reap_error = 0;
        std::array<std::optional<Error>, 3> stream_errors;
        const auto execute = [&]
        {
            try
            {
                fd_pair stdin_pipe, stdout_pipe, stderr_pipe, startup_pipe;
                std::array<fd_pair*, 4> pipes{&stdin_pipe, &stdout_pipe, &stderr_pipe, &startup_pipe};
                for (std::size_t index = 0; index < pipes.size(); ++index)
                {
                    if (auto error = make_pipe(*pipes[index], close_errors[index * 2], close_errors[index * 2 + 1]))
                    {
                        output.error = std::move(error);
                        return;
                    }
                }
                // Prepare owned argument storage before fork; the child only calls async-signal-safe APIs.
                std::vector<std::string> storage;
                storage.reserve(arguments.size() + 1);
                storage.push_back(executable.string());
                storage.insert(storage.end(), arguments.begin(), arguments.end());
                std::vector<char*> argv;
                argv.reserve(storage.size() + 1);
                for (auto& argument : storage)
                    argv.push_back(argument.data());
                argv.push_back(nullptr);
                const int stdin_read = stdin_pipe.read.get();
                const int stdout_write = stdout_pipe.write.get();
                const int stderr_write = stderr_pipe.write.get();
                const int failure_write = startup_pipe.write.get();
                const std::array<int, 7> child_closes{
                    stdin_read, stdin_pipe.write.get(), stdout_pipe.read.get(), stdout_write,
                    stderr_pipe.read.get(), stderr_write, startup_pipe.read.get()};
                const bool change_directory = !working_directory.empty();
                const char* directory = working_directory.c_str();
                const pid_t pid = ::fork();
                if (pid < 0)
                {
                    const int code = errno;
                    output.error = make_system_error("start_process", "fork", {code, std::generic_category()});
                    return;
                }
                if (pid == 0)
                {
                    if (::dup2(stdin_read, STDIN_FILENO) < 0)
                        report_child_failure(failure_write, child_operation::duplicate_stdin, errno, stdin_read);
                    if (::dup2(stdout_write, STDOUT_FILENO) < 0)
                        report_child_failure(failure_write, child_operation::duplicate_stdout, errno, stdout_write);
                    if (::dup2(stderr_write, STDERR_FILENO) < 0)
                        report_child_failure(failure_write, child_operation::duplicate_stderr, errno, stderr_write);
                    for (int descriptor : child_closes)
                    {
                        if (::close(descriptor) != 0)
                            report_child_failure(failure_write, child_operation::close_descriptor, errno, descriptor);
                    }
                    if (change_directory && ::chdir(directory) != 0)
                        report_child_failure(failure_write, child_operation::change_directory, errno);
                    ::execv(storage.front().c_str(), argv.data());
                    report_child_failure(failure_write, child_operation::execute, errno);
                }
                std::jthread stdin_writer, stdout_reader, stderr_reader;
                process_guard guard{pid, kill_error, reap_error};
                stdin_pipe.read.reset();
                stdout_pipe.write.reset();
                stderr_pipe.write.reset();
                startup_pipe.write.reset();
                child_failure failure{};
                std::size_t received = 0;
                for (;;)
                {
                    const ssize_t bytes = ::read(startup_pipe.read.get(),
                        reinterpret_cast<char*>(&failure) + received, sizeof(failure) - received);
                    if (bytes > 0)
                    {
                        received += static_cast<std::size_t>(bytes);
                        if (received == sizeof(failure))
                            break;
                        continue;
                    }
                    if (bytes == 0)
                        break;
                    const int code = errno;
                    if (code == EINTR)
                        continue;
                    output.error = make_system_error("read_process_startup", "read", {code, std::generic_category()});
                    return;
                }
                startup_pipe.read.reset();
                if (received != 0)
                {
                    if (received != sizeof(failure))
                        output.error = make_error("start_process", "protocol_error", "Incomplete child startup error",
                            {{"bytes_received", received}, {"bytes_expected", sizeof(failure)}});
                    else
                    {
                        const char* api = failure.operation == child_operation::change_directory ? "chdir"
                            : failure.operation == child_operation::execute ? "execv"
                            : failure.operation == child_operation::close_descriptor ? "close" : "dup2";
                        output.error = make_system_error("start_process", api, {failure.code, std::generic_category()},
                            {{"child_operation", static_cast<int>(failure.operation)}, {"descriptor", failure.descriptor},
                             {"path", path_text(failure.operation == child_operation::change_directory ? working_directory : executable)},
                             {"working_directory", path_text(working_directory)}});
                    }
                }
                else
                {
                    output.started = true;
                    stdin_writer = std::jthread(write_all, std::move(stdin_pipe.write), std::cref(stdin_data), std::ref(stream_errors[0]), std::ref(signal_errors));
                    stdout_reader = std::jthread(read_all, std::move(stdout_pipe.read), std::ref(output.stdout_text), std::ref(stream_errors[1]), "stdout");
                    stderr_reader = std::jthread(read_all, std::move(stderr_pipe.read), std::ref(output.stderr_text), std::ref(stream_errors[2]), "stderr");
                }
                int status = 0;
                pid_t waited;
                do { waited = ::waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
                if (waited < 0)
                {
                    const int code = errno;
                    append_error(output.error, make_system_error("wait_process", "waitpid", {code, std::generic_category()}));
                    if (code == ECHILD)
                        guard.active = false;
                    return;
                }
                guard.active = false;
                if (WIFEXITED(status))
                    output.exit_code = WEXITSTATUS(status);
                else if (WIFSIGNALED(status))
                    output.exit_code = 128 + WTERMSIG(status);
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
                append_error(output.error, make_system_error("close_process_descriptor", "close",
                    {close_errors[index], std::generic_category()}, {{"resource_index", index}}));
        }
        if (kill_error != 0)
            append_error(output.error, make_system_error("terminate_process", "kill", {kill_error, std::generic_category()}));
        if (reap_error != 0)
            append_error(output.error, make_system_error("reap_process", "waitpid", {reap_error, std::generic_category()}));
        if (signal_errors[0] != 0)
            append_error(output.error, make_system_error("drain_sigpipe", "sigtimedwait", {signal_errors[0], std::generic_category()}));
        if (signal_errors[1] != 0)
            append_error(output.error, make_system_error("restore_sigpipe", "pthread_sigmask", {signal_errors[1], std::generic_category()}));
        return output;
    }
}
