#include "job.h"
#include "../error_schema.h"

#include <stdexcept>
#include <system_error>

namespace sandbox::detail::process::windows
{
    namespace
    {
        [[noreturn]] void throw_last_error(const char* action)
        {
            sandbox::detail::throw_error(
                sandbox::detail::make_native_error(
                    action,
                    GetLastError()));
        }
    }

    unique_handle create_process_job()
    {
        unique_handle job = own_handle(CreateJobObjectW(nullptr, nullptr));
        if (!job)
            throw_last_error("CreateJobObjectW(sandbox process)");

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(
                static_cast<HANDLE>(job.get()),
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)))
        {
            throw_last_error("SetInformationJobObject(sandbox process)");
        }
        return job;
    }

    void assign_process_to_job(HANDLE job, HANDLE process)
    {
        if (!AssignProcessToJobObject(job, process))
            throw_last_error("AssignProcessToJobObject(sandbox process)");
    }
}
