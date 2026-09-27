#pragma once

#include <system_error>

#include <sys/types.h>

namespace sandbox::detail::linux
{
    class process_group final
    {
    public:
        explicit process_group(pid_t pgid) noexcept;

        process_group(const process_group&) = delete;
        process_group& operator=(const process_group&) = delete;

        ~process_group();

        [[nodiscard]] std::error_code terminate() const noexcept;

    private:
        pid_t pgid_ = -1;
    };
}
