#include "../sandbox/run.h"
#include "process/process_group.h"

#include <cerrno>
#include <csignal>
#include <string>
#include <string_view>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace sandbox::detail
{
    namespace
    {
        std::error_code errno_code(int value) noexcept
        {
            return {value, std::generic_category()};
        }

        [[noreturn]] void child_fail(int error) noexcept
        {
            _exit(error == 0 ? 127 : error);
        }

    }

    int run_body(std::string_view body)
    {
        const std::string script(body);
        const pid_t process = fork();
        if (process < 0)
            throw std::system_error(errno_code(errno), "fork(sandbox)");

        if (process == 0)
        {
            if (setpgid(0, 0) != 0)
                child_fail(errno);

            std::vector<char*> arguments{
                const_cast<char*>("pwsh"),
                const_cast<char*>("-NoLogo"),
                const_cast<char*>("-NoProfile"),
                const_cast<char*>("-NonInteractive"),
                const_cast<char*>("-Command"),
                const_cast<char*>(script.c_str()),
                nullptr,
            };
            execvp("pwsh", arguments.data());
            child_fail(errno);
        }

        linux::process_group process_tree(process);
        if (setpgid(process, process) != 0 && errno != EACCES && errno != ESRCH)
            throw std::system_error(errno_code(errno), "setpgid(sandbox)");

        int status = 0;
        for (;;)
        {
            if (waitpid(process, &status, 0) >= 0)
                break;
            if (errno == EINTR)
                continue;
            throw std::system_error(errno_code(errno), "waitpid(sandbox)");
        }

        const std::error_code terminate_error = process_tree.terminate();
        if (terminate_error)
        {
            throw std::system_error(
                terminate_error,
                "kill(sandbox process group)");
        }

        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status);
        throw std::system_error(
            std::make_error_code(std::errc::state_not_recoverable),
            "sandbox shell did not report an exit status");
    }
}
