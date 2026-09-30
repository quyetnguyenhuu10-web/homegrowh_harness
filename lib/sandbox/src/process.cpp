#include <sandbox>

#include "error_schema.h"

#if defined(_WIN32)
#include "window/process.h"
#elif defined(__linux__)
#include "linux/process.h"
#else
#error "Unsupported operating system"
#endif

namespace sandbox
{
    Result<void> process(process_request& request)
    {
        try
        {
#if defined(_WIN32)
            request.results = detail::process::windows::run(request);
#elif defined(__linux__)
            request.results = detail::process::linux::run(request);
#endif
        }
        catch (...)
        {
            request.results = {};
            request.results.state.final_error = detail::capture_exception(
                "run_process", std::current_exception());
        }

        const auto& state = request.results.state;
        std::vector<Error> errors;
        if (state.final_error)
            errors.push_back(*state.final_error);
        if (state.config.final_error)
            errors.push_back(*state.config.final_error);
        for (const auto& item : state.config.path_errors)
            errors.push_back(item.error);
        if (state.os_error_before_termination)
            errors.push_back(*state.os_error_before_termination);
        if (state.timed_out)
        {
            return Result<void>::failure(detail::make_error(
                "run_process", "timeout", "Sandbox process exceeded its timeout",
                {{"timeout_ms", request.timeout.count()}}, std::move(errors)));
        }
        if (errors.empty())
            return Result<void>::success();
        if (errors.size() == 1)
            return Result<void>::failure(std::move(errors.front()));
        return Result<void>::failure(detail::make_error(
            "run_process", "dependency_error", "Sandbox process encountered multiple errors",
            nullptr, std::move(errors)));
    }
}
