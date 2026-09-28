#pragma once

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace sandbox
{
    struct config_path_error
    {
        std::filesystem::path path;
        std::error_code error;
    };

    struct config_results
    {
        std::error_code final_error;
        std::vector<config_path_error> path_errors;
    };

    struct process_final_state
    {
        bool started = false;
        bool timed_out = false;
        bool terminated = false;
        int exit_code = -1;
        std::error_code os_error_before_termination;
        std::error_code final_error;
        config_results config;
    };

    struct process_results
    {
        process_final_state state;
        std::string stdout_text;
        std::string stderr_text;
    };
}
