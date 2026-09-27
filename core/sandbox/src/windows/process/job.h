#pragma once

#include "raii.h"

namespace sandbox::detail::windows
{
    class job final
    {
    public:
        job();

        job(const job&) = delete;
        job& operator=(const job&) = delete;

        job(job&&) noexcept = default;
        job& operator=(job&&) noexcept = default;
        ~job() = default;

        void assign(HANDLE process) const;

    private:
        unique_handle handle_;
    };
}
