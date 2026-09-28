#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace tool_runtime::detail::process
{
    struct result
    {
        bool started = false;
        int exit_code = -1;
        std::error_code error;
        std::string stdout_text;
        std::string stderr_text;
    };

    result run(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data);
}
