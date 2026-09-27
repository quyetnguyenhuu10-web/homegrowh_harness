#include "job.h"

#include <system_error>

namespace sandbox::detail::windows
{
    namespace
    {
        [[noreturn]] void throw_last_error(const char* action)
        {
            throw std::system_error(
                static_cast<int>(GetLastError()),
                std::system_category(),
                action);
        }
    }

    job::job()
        : handle_(own_handle(CreateJobObjectW(nullptr, nullptr)))
    {
        if (!handle_)
            throw_last_error("CreateJobObjectW(sandbox)");

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags =
            JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;

        if (!SetInformationJobObject(
                static_cast<HANDLE>(handle_.get()),
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)))
        {
            throw_last_error("SetInformationJobObject(sandbox)");
        }
    }

    void job::assign(HANDLE process) const
    {
        if (!AssignProcessToJobObject(
                static_cast<HANDLE>(handle_.get()),
                process))
        {
            throw_last_error("AssignProcessToJobObject(sandbox)");
        }
    }
}
