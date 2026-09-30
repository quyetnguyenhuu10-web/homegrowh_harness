#pragma once

#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

#include <tool_runtime/tool_runtime.h>

namespace tool_runtime::detail::process
{
    struct result
    {
        bool started = false;
        std::int64_t exit_code = -1;
        std::optional<Error> error;
        std::string stdout_text;
        std::string stderr_text;
    };

    result run(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data);

    result run_platform(
        const std::filesystem::path& executable,
        const std::vector<std::string>& arguments,
        const std::filesystem::path& working_directory,
        const std::string& stdin_data);
}
