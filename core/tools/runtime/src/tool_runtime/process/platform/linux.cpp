#include "linux.h"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <fcntl.h>
#include <pthread.h>
#include <string>
#include <thread>
#include <time.h>
#include <utility>
#include <vector>

namespace tool_runtime::detail::process_platform::linux
{
    namespace
    {
        class unique_fd final
        {
        public:
            unique_fd() noexcept = default;
            explicit unique_fd(int value) noexcept : value_(value) {}
            unique_fd(const unique_fd&) = delete;
            unique_fd& operator=(const unique_fd&) = delete;
            unique_fd(unique_fd&& other) noexcept
                : value_(std::exchange(other.value_, -1)) {}
            unique_fd& operator=(unique_fd&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    value_ = std::exchange(other.value_, -1);
                }
                return *this;
            }
            ~unique_fd() { reset(); }
            int get() const noexcept { return value_; }
            void reset() noexcept
            {
                if (value_ >= 0)
                {
                    close(value_);
                    value_ = -1;
                }
            }

        private:
            int value_ = -1;
        };

        struct pipe_pair final
        {
            unique_fd read;
            unique_fd write;
        };

        pipe_pair create_pipe()
        {
            int fds[2] = {-1, -1};
            if (pipe(fds) != 0)
                throw std::system_error(errno, std::generic_category(), "pipe(tool runtime)");
            return {unique_fd(fds[0]), unique_fd(fds[1])};
        }

        void read_pipe(unique_fd fd, std::string& output, std::atomic<int>& error) noexcept
        {
            std::array<char, 16 * 1024> buffer{};
            for (;;)
            {
                const ssize_t count = read(fd.get(), buffer.data(), buffer.size());
                if (count > 0)
                {
                    output.append(buffer.data(), static_cast<std::size_t>(count));
                    continue;
                }
                if (count == 0)
                    return;
                if (errno == EINTR)
                    continue;
                error.store(errno, std::memory_order_relaxed);
                return;
            }
        }

        void write_pipe(unique_fd fd, const std::string& input, std::atomic<int>& error) noexcept
        {
            sigset_t blocked{};
            sigset_t previous{};
            sigemptyset(&blocked);
            sigaddset(&blocked, SIGPIPE);
            pthread_sigmask(SIG_BLOCK, &blocked, &previous);

            const bool sigpipe_was_blocked = sigismember(&previous, SIGPIPE) == 1;
            bool saw_epipe = false;
            std::size_t offset = 0;
            while (offset < input.size())
            {
                const ssize_t count = write(
                    fd.get(),
                    input.data() + offset,
                    input.size() - offset);
                if (count > 0)
                {
                    offset += static_cast<std::size_t>(count);
                    continue;
                }
                if (count < 0 && errno == EINTR)
                    continue;
                if (count < 0 && errno == EPIPE)
                {
                    saw_epipe = true;
                    break;
                }
                error.store(count == 0 ? EIO : errno, std::memory_order_relaxed);
                break;
            }

            if (saw_epipe && !sigpipe_was_blocked)
            {
                const timespec timeout{};
                static_cast<void>(sigtimedwait(&blocked, nullptr, &timeout));
            }
            pthread_sigmask(SIG_SETMASK, &previous, nullptr);
        }

        int exit_code(int status) noexcept
        {
            if (WIFEXITED(status))
                return WEXITSTATUS(status);
            if (WIFSIGNALED(status))
                return 128 + WTERMSIG(status);
            return -1;
        }
    }

    process_result run(const process_request& request)
    {
        process_result result;
        try
        {
            pipe_pair input = create_pipe();
            pipe_pair output = create_pipe();
            pipe_pair error_output = create_pipe();

            const pid_t pid = fork();
            if (pid < 0)
                throw std::system_error(errno, std::generic_category(), "fork(tool runtime)");

            if (pid == 0)
            {
                if (setpgid(0, 0) != 0)
                    _exit(126);
                if (dup2(input.read.get(), STDIN_FILENO) < 0
                    || dup2(output.write.get(), STDOUT_FILENO) < 0
                    || dup2(error_output.write.get(), STDERR_FILENO) < 0)
                {
                    _exit(126);
                }
                if (!request.working_directory.empty()
                    && chdir(request.working_directory.c_str()) != 0)
                {
                    _exit(126);
                }

                std::vector<std::string> storage;
                storage.reserve(request.arguments.size() + 1);
                storage.push_back(request.executable.string());
                storage.insert(storage.end(), request.arguments.begin(), request.arguments.end());

                std::vector<char*> argv;
                argv.reserve(storage.size() + 1);
                for (std::string& item : storage)
                    argv.push_back(item.data());
                argv.push_back(nullptr);
                execv(request.executable.c_str(), argv.data());
                _exit(127);
            }

            result.started = true;
            input.read.reset();
            output.write.reset();
            error_output.write.reset();
            setpgid(pid, pid);

            std::atomic<int> io_error{0};
            std::jthread writer(write_pipe, std::move(input.write), std::cref(request.stdin_data), std::ref(io_error));
            std::jthread stdout_reader(read_pipe, std::move(output.read), std::ref(result.stdout_text), std::ref(io_error));
            std::jthread stderr_reader(read_pipe, std::move(error_output.read), std::ref(result.stderr_text), std::ref(io_error));

            const auto deadline = std::chrono::steady_clock::now()
                + (request.timeout.count() <= 0 ? std::chrono::milliseconds(1) : request.timeout);
            int status = 0;
            for (;;)
            {
                const pid_t waited = waitpid(pid, &status, WNOHANG);
                if (waited == pid)
                    break;
                if (waited < 0 && errno != EINTR)
                {
                    result.final_error = {errno, std::generic_category()};
                    kill(-pid, SIGKILL);
                    result.terminated = true;
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline)
                {
                    result.timed_out = true;
                    if (kill(-pid, SIGKILL) != 0 && errno != ESRCH)
                        result.final_error = {errno, std::generic_category()};
                    result.terminated = true;
                    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            result.exit_code = exit_code(status);
            writer.join();
            stdout_reader.join();
            stderr_reader.join();
            const int io = io_error.load(std::memory_order_relaxed);
            if (io != 0 && !result.final_error)
                result.final_error = {io, std::generic_category()};
            return result;
        }
        catch (const std::system_error& error)
        {
            result.final_error = error.code();
            return result;
        }
    }
}
