#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace tool_runtime::detail
{
    struct process_request final
    {
        std::filesystem::path executable;
        std::vector<std::string> arguments;
        std::filesystem::path working_directory;
        std::string stdin_data;
        std::chrono::milliseconds timeout{120000};
    };

    struct process_result final
    {
        bool started = false;
        bool timed_out = false;
        bool terminated = false;
        int exit_code = -1;
        std::error_code final_error;
        std::string stdout_text;
        std::string stderr_text;
    };

    process_result run_process(const process_request& request);
}
