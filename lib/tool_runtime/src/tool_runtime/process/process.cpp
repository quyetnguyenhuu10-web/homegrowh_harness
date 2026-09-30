#include "process.h"

#include "../error/error.h"
#include "../platform/platform.h"

#include <algorithm>

namespace tool_runtime::detail::process
{
    result run(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data)
    {
        result output;
        try
        {
            for (const auto* path : {&executable, &working_directory})
            {
                const auto& native = path->native();
                if (std::find(native.begin(), native.end(), std::filesystem::path::value_type{}) != native.end())
                {
                    output.error = make_error("start_process", "validation_error", "Process path contains a null character",
                        {{"path", path_text(*path)}});
                    return output;
                }
            }
            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                if (arguments[index].find('\0') != std::string::npos)
                {
                    output.error = make_error("start_process", "validation_error", "Process argument contains a null character",
                        {{"argument_index", index}, {"argument", text_payload(arguments[index])}});
                    return output;
                }
            }
            output = run_platform(executable, arguments, working_directory, stdin_data);
            if (output.error)
            {
                output.error->data.push_back({
                    {"path", path_text(executable)},
                    {"working_directory", path_text(working_directory)}
                });
            }
        }
        catch (...)
        {
            append_error(output.error, current_exception_error("run_process"));
        }
        return output;
    }
}
