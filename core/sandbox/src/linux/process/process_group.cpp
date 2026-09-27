#include "process_group.h"

#include <cerrno>
#include <csignal>

namespace sandbox::detail::linux
{
    process_group::process_group(pid_t pgid) noexcept
        : pgid_(pgid)
    {
    }

    process_group::~process_group()
    {
        static_cast<void>(terminate());
    }

    std::error_code process_group::terminate() const noexcept
    {
        if (pgid_ <= 0)
            return {};

        if (kill(-pgid_, SIGKILL) == 0)
            return {};

        const int error = errno;
        if (error == ESRCH)
            return {};
        return {error, std::generic_category()};
    }
}
