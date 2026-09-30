#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "process.h"

#include "../apply_config.h"
#include "../error_schema.h"
#include "../process/io_failure.h"
#include "landlock.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
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
        [[noreturn]] void throw_errno(
            const char* operation,
            int error = errno)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_system_error(
                    operation,
                    std::error_code(error, std::generic_category())));
        }

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

        enum class child_error_stage : std::uint32_t
        {
            set_process_group = 1,
            set_parent_death_signal = 2,
            duplicate_stdin = 3,
            duplicate_stdout = 4,
            duplicate_stderr = 5,
            change_directory = 6,
            apply_landlock = 7,
            execute = 8,
        };

        struct child_error_packet
        {
            child_error_stage stage{};
            int error = 0;
            std::uint32_t detail = 0;
            std::uint32_t permission_index = static_cast<std::uint32_t>(-1);
            int cleanup_error = 0;
            std::uint32_t cleanup_detail = 0;
            int native_result = 0;
        };

        const char* landlock_operation(landlock_error_stage stage) noexcept
        {
            switch (stage)
            {
            case landlock_error_stage::unsupported:
                return "landlock(unsupported)";
            case landlock_error_stage::query_abi:
                return "landlock_create_ruleset(query_abi)";
            case landlock_error_stage::create_ruleset:
                return "landlock_create_ruleset";
            case landlock_error_stage::inspect_path:
                return "stat(landlock_path)";
            case landlock_error_stage::open_path:
                return "open(landlock_path)";
            case landlock_error_stage::add_rule:
                return "landlock_add_rule";
            case landlock_error_stage::set_no_new_privileges:
                return "prctl(PR_SET_NO_NEW_PRIVS)";
            case landlock_error_stage::restrict_self:
                return "landlock_restrict_self";
            case landlock_error_stage::close_path:
                return "close(landlock_path)";
            case landlock_error_stage::close_ruleset:
                return "close(landlock_ruleset)";
            case landlock_error_stage::none:
                break;
            }
            return "apply_landlock";
        }

        bool landlock_stage_uses_path(landlock_error_stage stage) noexcept
        {
            return stage == landlock_error_stage::inspect_path
                || stage == landlock_error_stage::open_path
                || stage == landlock_error_stage::add_rule
                || stage == landlock_error_stage::close_path;
        }

        Error child_setup_error(
            const child_error_packet& packet,
            const process_request& request,
            const registry_result& registry)
        {
            const auto system_error = [&](const char* operation)
            {
                Error error = sandbox::detail::make_system_error(
                    operation,
                    std::error_code(packet.error, std::generic_category()));
                error.data.push_back({
                    {"stage", static_cast<std::uint32_t>(packet.stage)},
                });
                return error;
            };

            switch (packet.stage)
            {
            case child_error_stage::set_process_group:
                return system_error("setpgid");
            case child_error_stage::set_parent_death_signal:
                return system_error("prctl(PR_SET_PDEATHSIG)");
            case child_error_stage::duplicate_stdin:
                return system_error("dup2(stdin)");
            case child_error_stage::duplicate_stdout:
                return system_error("dup2(stdout)");
            case child_error_stage::duplicate_stderr:
                return system_error("dup2(stderr)");
            case child_error_stage::change_directory:
                return sandbox::detail::make_system_error(
                    "chdir",
                    std::error_code(packet.error, std::generic_category()),
                    request.working_directory);
            case child_error_stage::apply_landlock:
            {
                const auto make_landlock_failure = [&](int code, std::uint32_t detail)
                {
                    const auto stage = static_cast<landlock_error_stage>(detail);
                    const bool has_path = landlock_stage_uses_path(stage)
                        && packet.permission_index
                            != static_cast<std::uint32_t>(-1)
                        && packet.permission_index < registry.permissions.size();
                    Error error = has_path
                        ? sandbox::detail::make_system_error(
                            landlock_operation(stage),
                            std::error_code(code, std::generic_category()),
                            registry.permissions[packet.permission_index].path)
                        : sandbox::detail::make_system_error(
                            landlock_operation(stage),
                            std::error_code(code, std::generic_category()));
                    error.data.push_back({
                        {"landlock_stage", detail},
                        {"permission_index", packet.permission_index},
                    });
                    if (stage == landlock_error_stage::query_abi
                        || stage == landlock_error_stage::unsupported)
                    {
                        if (code == 0)
                        {
                            error.type = stage == landlock_error_stage::unsupported
                                ? "dependency_error" : "protocol_error";
                            error.message = stage == landlock_error_stage::unsupported
                                ? "Landlock syscall support is unavailable in this build"
                                : "Landlock returned an invalid ABI version";
                            error.data.front().erase("code");
                            error.data.front().erase("category");
                        }
                        if (stage == landlock_error_stage::query_abi)
                            error.data.push_back({{"native_result", packet.native_result}});
                    }
                    return error;
                };

                Error primary = make_landlock_failure(packet.error, packet.detail);
                if (packet.cleanup_error == 0)
                    return primary;

                Error cleanup = make_landlock_failure(
                    packet.cleanup_error,
                    packet.cleanup_detail);
                return sandbox::detail::make_error(
                    "apply_landlock",
                    "operation_failed",
                    "Landlock setup failed and cleanup also failed",
                    nullptr,
                    {std::move(primary), std::move(cleanup)});
            }
            case child_error_stage::execute:
                return sandbox::detail::make_system_error(
                    "execv",
                    std::error_code(packet.error, std::generic_category()),
                    request.executable);
            }

            return sandbox::detail::make_error(
                "child_setup_error_pipe",
                "protocol_error",
                "sandbox child returned an unknown setup error stage",
                {{{"stage", static_cast<std::uint32_t>(packet.stage)},
                  {"code", packet.error}}});
        }

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
                    throw_errno("sigaction(SIGTERM)");
                }
                term_installed_ = true;

                if (sigaction(SIGINT, &action, &old_int_) != 0)
                {
                    const int error = errno;
                    Error primary = sandbox::detail::make_system_error(
                        "sigaction(SIGINT)",
                        std::error_code(error, std::generic_category()));
                    if (sigaction(SIGTERM, &old_term_, nullptr) != 0)
                    {
                        Error cleanup = sandbox::detail::make_system_error(
                            "sigaction(SIGTERM restore)",
                            std::error_code(errno, std::generic_category()));
                        sandbox::detail::throw_error(
                            sandbox::detail::make_error(
                                "install_signal_handlers",
                                "operation_failed",
                                "installing SIGINT handler failed and SIGTERM rollback also failed",
                                nullptr,
                                {std::move(primary), std::move(cleanup)}));
                    }
                    term_installed_ = false;
                    sandbox::detail::throw_error(std::move(primary));
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
            child_process_guard(
                pid_t pid,
                int* kill_error,
                int* wait_error) noexcept
                : pid_(pid),
                  kill_error_(kill_error),
                  wait_error_(wait_error)
            {
            }

            child_process_guard(const child_process_guard&) = delete;
            child_process_guard& operator=(const child_process_guard&) = delete;

            ~child_process_guard()
            {
                if (!active_ || pid_ <= 0)
                    return;

                if (kill(-pid_, SIGKILL) != 0
                    && errno != ESRCH
                    && kill_error_ != nullptr)
                {
                    *kill_error_ = errno;
                }
                int status = 0;
                int waited = 0;
                do
                {
                }
                while ((waited = waitpid(pid_, &status, 0)) < 0 && errno == EINTR);
                if (waited < 0 && errno != ECHILD && wait_error_ != nullptr)
                    *wait_error_ = errno;
            }

            void release() noexcept
            {
                active_ = false;
            }

        private:
            pid_t pid_ = -1;
            int* kill_error_ = nullptr;
            int* wait_error_ = nullptr;
            bool active_ = true;
        };

        Error attach_child_guard_cleanup_error(
            Error primary,
            int kill_error,
            int wait_error)
        {
            if (kill_error == 0 && wait_error == 0)
                return primary;

            std::vector<Error> causes;
            causes.reserve(3);
            causes.push_back(std::move(primary));
            if (kill_error != 0)
            {
                causes.push_back(sandbox::detail::make_system_error(
                    "kill(SIGKILL)",
                    std::error_code(kill_error, std::generic_category())));
            }
            if (wait_error != 0)
            {
                causes.push_back(sandbox::detail::make_system_error(
                    "waitpid(child_guard)",
                    std::error_code(wait_error, std::generic_category())));
            }

            return sandbox::detail::make_error(
                "abort_child_process",
                "operation_failed",
                "sandbox child cleanup failed while unwinding another failure",
                nullptr,
                std::move(causes));
        }

        pipe_pair create_pipe()
        {
            int descriptors[2] = {-1, -1};
            if (pipe2(descriptors, O_CLOEXEC) != 0)
            {
                throw_errno("pipe2");
            }
            return {
                unique_fd(descriptors[0]),
                unique_fd(descriptors[1]),
            };
        }

        void read_pipe(
            unique_fd descriptor,
            std::string& output,
            sandbox::detail::io_failure<int>& io_error) noexcept
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
                    try
                    {
                        output.append(buffer.data(), static_cast<std::size_t>(bytes));
                    }
                    catch (...)
                    {
                        io_error.record_exception();
                        return;
                    }
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
            sandbox::detail::io_failure<int>& io_error) noexcept
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
                if (bytes == 0)
                    io_error.record_no_progress(offset, input.size() - offset);
                else
                    io_error.store(errno, std::memory_order_relaxed);
                return;
            }
        }

        std::optional<Error> observed_io_error(
            const char* operation,
            const sandbox::detail::io_failure<int>& source)
        {
            return source.observe(operation, std::generic_category());
        }

        std::optional<Error> collect_io_errors(
            const sandbox::detail::io_failure<int>& stdin_error,
            const sandbox::detail::io_failure<int>& stdout_error,
            const sandbox::detail::io_failure<int>& stderr_error,
            const std::optional<Error>& observed = std::nullopt)
        {
            std::vector<Error> errors;
            if (auto error = observed_io_error("write(stdin)", stdin_error))
            {
                if (!sandbox::detail::io_already_observed(*error, observed))
                    errors.push_back(std::move(*error));
            }
            if (auto error = observed_io_error("read(stdout)", stdout_error))
            {
                if (!sandbox::detail::io_already_observed(*error, observed))
                    errors.push_back(std::move(*error));
            }
            if (auto error = observed_io_error("read(stderr)", stderr_error))
            {
                if (!sandbox::detail::io_already_observed(*error, observed))
                    errors.push_back(std::move(*error));
            }

            if (errors.empty())
                return std::nullopt;
            if (errors.size() == 1)
                return std::move(errors.front());
            return sandbox::detail::make_error(
                "process_io",
                "operation_failed",
                "multiple sandbox process I/O operations failed",
                nullptr,
                std::move(errors));
        }

        void merge_failure(std::optional<Error>& destination, Error error)
        {
            if (!destination)
            {
                destination = std::move(error);
                return;
            }

            std::vector<Error> causes;
            causes.reserve(2);
            causes.push_back(std::move(*destination));
            causes.push_back(std::move(error));
            destination = sandbox::detail::make_error(
                "run_process",
                "operation_failed",
                "multiple sandbox process operations failed",
                nullptr,
                std::move(causes));
        }

        void write_child_error(
            int descriptor,
            child_error_stage stage,
            int error,
            std::uint32_t detail = 0,
            std::uint32_t permission_index = static_cast<std::uint32_t>(-1),
            int cleanup_error = 0,
            std::uint32_t cleanup_detail = 0,
            int native_result = 0) noexcept
        {
            const child_error_packet packet{
                stage,
                error,
                detail,
                permission_index,
                cleanup_error,
                cleanup_detail,
                native_result,
            };
            const auto* data = reinterpret_cast<const char*>(&packet);
            std::size_t offset = 0;
            while (offset < sizeof(packet))
            {
                const ssize_t bytes = write(
                    descriptor,
                    data + offset,
                    sizeof(packet) - offset);
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
                write_child_error(
                    error_fd,
                    child_error_stage::set_process_group,
                    errno);
                _exit(126);
            }
            if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0)
            {
                write_child_error(
                    error_fd,
                    child_error_stage::set_parent_death_signal,
                    errno);
                _exit(126);
            }

            if (dup2(stdin_pipe.read.get(), STDIN_FILENO) < 0)
            {
                write_child_error(
                    error_fd,
                    child_error_stage::duplicate_stdin,
                    errno);
                _exit(126);
            }
            if (dup2(stdout_pipe.write.get(), STDOUT_FILENO) < 0)
            {
                write_child_error(
                    error_fd,
                    child_error_stage::duplicate_stdout,
                    errno);
                _exit(126);
            }
            if (dup2(stderr_pipe.write.get(), STDERR_FILENO) < 0)
            {
                write_child_error(
                    error_fd,
                    child_error_stage::duplicate_stderr,
                    errno);
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
                write_child_error(
                    error_fd,
                    child_error_stage::change_directory,
                    errno);
                _exit(126);
            }

            const landlock_error landlock_failure = apply_landlock(registry);
            if (landlock_failure)
            {
                write_child_error(
                    error_fd,
                    child_error_stage::apply_landlock,
                    landlock_failure.code,
                    static_cast<std::uint32_t>(landlock_failure.stage),
                    landlock_failure.permission_index
                        == static_cast<std::size_t>(-1)
                        ? static_cast<std::uint32_t>(-1)
                        : static_cast<std::uint32_t>(
                            landlock_failure.permission_index),
                    landlock_failure.cleanup_code,
                    static_cast<std::uint32_t>(
                        landlock_failure.cleanup_stage),
                    landlock_failure.native_result);
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
            write_child_error(
                error_fd,
                child_error_stage::execute,
                errno);
            _exit(127);
        }
    }

    process_results run(const process_request& request)
    {
        process_results result;
        int child_guard_kill_error = 0;
        int child_guard_wait_error = 0;
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
                throw_errno("fork");
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

            child_process_guard child_guard(
                pid,
                &child_guard_kill_error,
                &child_guard_wait_error);
            result.state.started = true;
            stdin_pipe.read.reset();
            stdout_pipe.write.reset();
            stderr_pipe.write.reset();
            error_pipe.write.reset();
            setpgid(pid, pid);

            const auto fail_parent_setup = [&](const char* operation, int error)
            {
                Error primary = sandbox::detail::make_system_error(
                    operation,
                    std::error_code(error, std::generic_category()));
                std::vector<Error> cleanup_errors;

                if (kill(-pid, SIGKILL) != 0 && errno != ESRCH)
                {
                    cleanup_errors.push_back(sandbox::detail::make_system_error(
                        "kill(SIGKILL)",
                        std::error_code(errno, std::generic_category())));
                }

                int waited = 0;
                do
                {
                    waited = waitpid(pid, nullptr, 0);
                }
                while (waited < 0 && errno == EINTR);
                if (waited < 0 && errno != ECHILD)
                {
                    cleanup_errors.push_back(sandbox::detail::make_system_error(
                        "waitpid(after setup failure)",
                        std::error_code(errno, std::generic_category())));
                }

                child_guard.release();
                result.state.terminated = true;
                if (cleanup_errors.empty())
                {
                    result.state.final_error = std::move(primary);
                    return;
                }

                std::vector<Error> causes;
                causes.reserve(1 + cleanup_errors.size());
                causes.push_back(std::move(primary));
                for (Error& cleanup : cleanup_errors)
                    causes.push_back(std::move(cleanup));
                result.state.final_error = sandbox::detail::make_error(
                    "abort_process_after_setup_failure",
                    "operation_failed",
                    "sandbox process setup failed and cleanup also failed",
                    nullptr,
                    std::move(causes));
            };

            const int current_flags = fcntl(error_pipe.read.get(), F_GETFL, 0);
            if (current_flags < 0)
            {
                const int error = errno;
                fail_parent_setup("fcntl(F_GETFL)", error);
                return result;
            }
            if (fcntl(
                    error_pipe.read.get(),
                    F_SETFL,
                    current_flags | O_NONBLOCK) != 0)
            {
                const int error = errno;
                fail_parent_setup("fcntl(F_SETFL)", error);
                return result;
            }

            sandbox::detail::io_failure<int> stdin_error{0};
            sandbox::detail::io_failure<int> stdout_error{0};
            sandbox::detail::io_failure<int> stderr_error{0};
            joining_thread stdin_writer(
                write_pipe,
                std::move(stdin_pipe.write),
                std::cref(request.stdin_data),
                std::ref(stdin_error));
            joining_thread stdout_reader(
                read_pipe,
                std::move(stdout_pipe.read),
                std::ref(result.stdout_text),
                std::ref(stdout_error));
            joining_thread stderr_reader(
                read_pipe,
                std::move(stderr_pipe.read),
                std::ref(result.stderr_text),
                std::ref(stderr_error));

            child_error_packet child_setup_failure{};
            bool child_setup_error_received = false;
            const auto consume_child_setup_error = [&]() {
                if (child_setup_error_received || error_pipe.read.get() < 0)
                    return;
                const ssize_t bytes = read(
                    error_pipe.read.get(),
                    &child_setup_failure,
                    sizeof(child_setup_failure));
                if (bytes == static_cast<ssize_t>(sizeof(child_setup_failure)))
                {
                    child_setup_error_received = true;
                    merge_failure(result.state.final_error, child_setup_error(
                        child_setup_failure,
                        request,
                        applied.filesystem));
                    return;
                }
                if (bytes > 0)
                {
                    child_setup_error_received = true;
                    merge_failure(result.state.final_error, sandbox::detail::make_error(
                        "child_setup_error_pipe",
                        "protocol_error",
                        "sandbox child setup error packet was incomplete",
                        {{{"bytes", bytes},
                          {"expected_bytes", sizeof(child_setup_failure)},
                          {"packet", std::vector<unsigned char>(
                              reinterpret_cast<const unsigned char*>(&child_setup_failure),
                              reinterpret_cast<const unsigned char*>(&child_setup_failure) + bytes)}}}));
                    return;
                }
                if (bytes < 0
                    && errno != EAGAIN
                    && errno != EWOULDBLOCK
                    && errno != EINTR)
                {
                    child_setup_error_received = true;
                    merge_failure(result.state.final_error, sandbox::detail::make_system_error(
                        "read(child_setup_error_pipe)",
                        std::error_code(
                            errno,
                            std::generic_category())));
                }
            };

            const auto terminate_child = [&]()
            {
                if (kill(-pid, SIGKILL) != 0 && errno != ESRCH)
                {
                    const int error = errno;
                    merge_failure(result.state.final_error, sandbox::detail::make_system_error(
                        "kill", std::error_code(error, std::generic_category())));
                }
                result.state.terminated = true;
            };
            const auto wait_for_child = [&](int& status)
            {
                pid_t waited = -1;
                do
                {
                    waited = waitpid(pid, &status, 0);
                } while (waited < 0 && errno == EINTR);
                if (waited < 0)
                {
                    const int error = errno;
                    merge_failure(result.state.final_error, sandbox::detail::make_system_error(
                        "waitpid(after termination)",
                        std::error_code(error, std::generic_category())));
                }
                return waited == pid;
            };

            const auto deadline = std::chrono::steady_clock::now()
                + (request.timeout.count() <= 0
                    ? std::chrono::milliseconds(1)
                    : request.timeout);

            int status = 0;
            bool exit_status_available = false;
            for (;;)
            {
                consume_child_setup_error();
                const pid_t waited = waitpid(pid, &status, WNOHANG);
                if (waited == pid)
                {
                    exit_status_available = true;
                    break;
                }
                if (waited < 0 && errno != EINTR)
                {
                    merge_failure(result.state.final_error, sandbox::detail::make_system_error(
                        "waitpid",
                        std::error_code(
                            errno,
                            std::generic_category())));
                    terminate_child();
                    exit_status_available = wait_for_child(status);
                    break;
                }

                if (stop_requested != 0)
                {
                    const auto signal = stop_requested;
                    merge_failure(result.state.final_error, sandbox::detail::make_error(
                        "process_cancel", "cancelled", "Sandbox process received a stop signal",
                        {{"signal", signal}}));
                    terminate_child();
                    exit_status_available = wait_for_child(status);
                    break;
                }

                if (std::chrono::steady_clock::now() >= deadline)
                {
                    result.state.timed_out = true;
                    result.state.os_error_before_termination = collect_io_errors(
                        stdin_error,
                        stdout_error,
                        stderr_error);

                    terminate_child();
                    exit_status_available = wait_for_child(status);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            child_guard.release();
            consume_child_setup_error();
            error_pipe.read.reset();
            if (exit_status_available)
                result.state.exit_code = child_exit_code(status);
            stdin_writer.join();
            stdout_reader.join();
            stderr_reader.join();
            if (auto io_failure = collect_io_errors(
                    stdin_error,
                    stdout_error,
                    stderr_error,
                    result.state.os_error_before_termination))
            {
                merge_failure(result.state.final_error, std::move(*io_failure));
            }
            return result;
        }
        catch (...)
        {
            result.state.final_error = attach_child_guard_cleanup_error(
                sandbox::detail::capture_exception(
                    "run_process", std::current_exception()),
                child_guard_kill_error,
                child_guard_wait_error);
            return result;
        }
    }
}
