#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "process.h"

#include "../apply_config.h"
#include "landlock.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/prctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace sandbox::detail::process::linux
{
    namespace
    {
        class unique_fd final
        {
        public:
            unique_fd() = default;
            explicit unique_fd(int value) noexcept : value_(value) {}
            unique_fd(const unique_fd&) = delete;
            unique_fd& operator=(const unique_fd&) = delete;

            unique_fd(unique_fd&& other) noexcept
                : value_(other.release())
            {
            }

            unique_fd& operator=(unique_fd&& other) noexcept
            {
                if (this != &other)
                    reset(other.release());
                return *this;
            }

            ~unique_fd()
            {
                reset();
            }

            int get() const noexcept { return value_; }

            int release() noexcept
            {
                const int value = value_;
                value_ = -1;
                return value;
            }

            void reset(int next = -1) noexcept
            {
                if (value_ >= 0)
                    close(value_);
                value_ = next;
            }

        private:
            int value_ = -1;
        };

        struct pipe_pair
        {
            unique_fd read;
            unique_fd write;
        };

        volatile std::sig_atomic_t stop_requested = 0;

        void request_stop(int) noexcept
        {
            stop_requested = 1;
        }

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

        class signal_scope final
        {
        public:
            signal_scope()
            {
                struct sigaction action{};
                action.sa_handler = request_stop;
                sigemptyset(&action.sa_mask);

                if (sigaction(SIGTERM, &action, &old_term_) != 0)
                {
                    throw std::system_error(
                        errno,
                        std::generic_category(),
                        "sigaction(SIGTERM)");
                }
                term_installed_ = true;

                if (sigaction(SIGINT, &action, &old_int_) != 0)
                {
                    const int error = errno;
                    sigaction(SIGTERM, &old_term_, nullptr);
                    term_installed_ = false;
                    throw std::system_error(
                        error,
                        std::generic_category(),
                        "sigaction(SIGINT)");
                }
                int_installed_ = true;
            }

            signal_scope(const signal_scope&) = delete;
            signal_scope& operator=(const signal_scope&) = delete;

            ~signal_scope()
            {
                if (int_installed_)
                    sigaction(SIGINT, &old_int_, nullptr);
                if (term_installed_)
                    sigaction(SIGTERM, &old_term_, nullptr);
            }

        private:
            struct sigaction old_term_{};
            struct sigaction old_int_{};
            bool term_installed_ = false;
            bool int_installed_ = false;
        };

        class child_process_guard final
        {
        public:
            explicit child_process_guard(pid_t pid) noexcept
                : pid_(pid)
            {
            }

            child_process_guard(const child_process_guard&) = delete;
            child_process_guard& operator=(const child_process_guard&) = delete;

            ~child_process_guard()
            {
                if (!active_ || pid_ <= 0)
                    return;

                kill(-pid_, SIGKILL);
                int status = 0;
                while (waitpid(pid_, &status, 0) < 0 && errno == EINTR)
                {
                }
            }

            void release() noexcept
            {
                active_ = false;
            }

        private:
            pid_t pid_ = -1;
            bool active_ = true;
        };

        pipe_pair create_pipe()
        {
            int descriptors[2] = {-1, -1};
            if (pipe2(descriptors, O_CLOEXEC) != 0)
            {
                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "pipe2(sandbox process)");
            }
            return {
                unique_fd(descriptors[0]),
                unique_fd(descriptors[1]),
            };
        }

        void read_pipe(
            unique_fd descriptor,
            std::string& output,
            std::atomic<int>& io_error) noexcept
        {
            std::array<char, 8192> buffer{};
            for (;;)
            {
                const ssize_t bytes = read(
                    descriptor.get(),
                    buffer.data(),
                    buffer.size());
                if (bytes > 0)
                {
                    output.append(buffer.data(), static_cast<std::size_t>(bytes));
                    continue;
                }
                if (bytes == 0)
                    return;
                if (errno == EINTR)
                    continue;
                io_error.store(errno, std::memory_order_relaxed);
                return;
            }
        }

        void write_pipe(
            unique_fd descriptor,
            const std::string& input,
            std::atomic<int>& io_error) noexcept
        {
            std::size_t offset = 0;
            while (offset < input.size())
            {
                const ssize_t bytes = write(
                    descriptor.get(),
                    input.data() + offset,
                    input.size() - offset);
                if (bytes > 0)
                {
                    offset += static_cast<std::size_t>(bytes);
                    continue;
                }
                if (bytes < 0 && errno == EINTR)
                    continue;
                io_error.store(bytes == 0 ? EIO : errno, std::memory_order_relaxed);
                return;
            }
        }

        void write_child_error(int descriptor, int error) noexcept
        {
            const auto* data = reinterpret_cast<const char*>(&error);
            std::size_t offset = 0;
            while (offset < sizeof(error))
            {
                const ssize_t bytes = write(
                    descriptor,
                    data + offset,
                    sizeof(error) - offset);
                if (bytes > 0)
                {
                    offset += static_cast<std::size_t>(bytes);
                    continue;
                }
                if (bytes < 0 && errno == EINTR)
                    continue;
                return;
            }
        }

        int child_exit_code(int status) noexcept
        {
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            if (WIFSIGNALED(status))
                return 128 + WTERMSIG(status);
            return -1;
        }

        [[noreturn]] void run_child(
            const process_request& request,
            const registry_result& registry,
            pipe_pair& stdin_pipe,
            pipe_pair& stdout_pipe,
            pipe_pair& stderr_pipe,
            pipe_pair& error_pipe) noexcept
        {
            const int error_fd = error_pipe.write.get();

            if (setpgid(0, 0) != 0)
            {
                write_child_error(error_fd, errno);
                _exit(126);
            }
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0)
            {
                write_child_error(error_fd, errno);
                _exit(126);
            }

            if (dup2(stdin_pipe.read.get(), STDIN_FILENO) < 0
                || dup2(stdout_pipe.write.get(), STDOUT_FILENO) < 0
                || dup2(stderr_pipe.write.get(), STDERR_FILENO) < 0)
            {
                write_child_error(error_fd, errno);
                _exit(126);
            }

            stdin_pipe.read.reset();
            stdin_pipe.write.reset();
            stdout_pipe.read.reset();
            stdout_pipe.write.reset();
            stderr_pipe.read.reset();
            stderr_pipe.write.reset();
            error_pipe.read.reset();

            if (!request.working_directory.empty()
                && chdir(request.working_directory.c_str()) != 0)
            {
                write_child_error(error_fd, errno);
                _exit(126);
            }

            const std::error_code landlock_error = apply_landlock(registry);
            if (landlock_error)
            {
                write_child_error(error_fd, landlock_error.value());
                _exit(126);
            }

            const std::string executable = request.executable.string();
            std::vector<std::string> argument_storage;
            argument_storage.reserve(1);
            argument_storage.push_back(executable);

            std::vector<char*> argv;
            argv.reserve(argument_storage.size() + 1);
            for (std::string& argument : argument_storage)
                argv.push_back(argument.data());
            argv.push_back(nullptr);

            execv(executable.c_str(), argv.data());
            write_child_error(error_fd, errno);
            _exit(127);
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

            pipe_pair stdin_pipe = create_pipe();
            pipe_pair stdout_pipe = create_pipe();
            pipe_pair stderr_pipe = create_pipe();
            pipe_pair error_pipe = create_pipe();

            stop_requested = 0;
            signal_scope signals;

            const pid_t pid = fork();
            if (pid < 0)
            {
                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "fork(sandbox process)");
            }
            if (pid == 0)
            {
                run_child(
                    request,
                    applied.filesystem,
                    stdin_pipe,
                    stdout_pipe,
                    stderr_pipe,
                    error_pipe);
            }

            child_process_guard child_guard(pid);
            result.state.started = true;
            stdin_pipe.read.reset();
            stdout_pipe.write.reset();
            stderr_pipe.write.reset();
            error_pipe.write.reset();
            setpgid(pid, pid);

            const int current_flags = fcntl(error_pipe.read.get(), F_GETFL, 0);
            if (current_flags < 0
                || fcntl(
                    error_pipe.read.get(),
                    F_SETFL,
                    current_flags | O_NONBLOCK) != 0)
            {
                const int error = errno;
                kill(-pid, SIGKILL);
                while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR)
                {
                }
                child_guard.release();
                result.state.terminated = true;
                result.state.final_error = std::error_code(
                    error,
                    std::generic_category());
                return result;
            }

            std::atomic<int> io_error{0};
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

            int child_setup_error = 0;
            bool child_setup_error_received = false;
            int last_os_error = 0;
            const auto consume_child_setup_error = [&]() noexcept {
                if (child_setup_error_received || error_pipe.read.get() < 0)
                    return;
                const ssize_t bytes = read(
                    error_pipe.read.get(),
                    &child_setup_error,
                    sizeof(child_setup_error));
                if (bytes == static_cast<ssize_t>(sizeof(child_setup_error)))
                {
                    child_setup_error_received = true;
                    result.state.final_error = std::error_code(
                        child_setup_error,
                        std::generic_category());
                    return;
                }
                if (bytes < 0
                    && errno != EAGAIN
                    && errno != EWOULDBLOCK
                    && errno != EINTR)
                {
                    last_os_error = errno;
                }
            };

            const auto deadline = std::chrono::steady_clock::now()
                + (request.timeout.count() <= 0
                    ? std::chrono::milliseconds(1)
                    : request.timeout);

            int status = 0;
            for (;;)
            {
                consume_child_setup_error();
                const pid_t waited = waitpid(pid, &status, WNOHANG);
                if (waited == pid)
                    break;
                if (waited < 0 && errno != EINTR)
                {
                    last_os_error = errno;
                    result.state.final_error = std::error_code(
                        last_os_error,
                        std::generic_category());
                    kill(-pid, SIGKILL);
                    result.state.terminated = true;
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                    {
                    }
                    break;
                }

                if (stop_requested != 0)
                {
                    kill(-pid, SIGKILL);
                    result.state.terminated = true;
                    result.state.final_error = std::error_code(
                        ECANCELED,
                        std::generic_category());
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                    {
                    }
                    break;
                }

                if (std::chrono::steady_clock::now() >= deadline)
                {
                    int captured = last_os_error;
                    if (captured == 0)
                        captured = io_error.load(std::memory_order_relaxed);

                    result.state.timed_out = true;
                    result.state.os_error_before_termination = std::error_code(
                        captured,
                        std::generic_category());

                    if (kill(-pid, SIGKILL) != 0 && errno != ESRCH)
                    {
                        result.state.final_error = std::error_code(
                            errno,
                            std::generic_category());
                    }
                    result.state.terminated = true;
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
                    {
                    }
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            child_guard.release();
            consume_child_setup_error();
            error_pipe.read.reset();
            result.state.exit_code = child_exit_code(status);
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
