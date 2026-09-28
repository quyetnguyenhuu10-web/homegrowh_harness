#pragma once

#include <config.h>
#include <process_results.h>

#include <chrono>
#include <filesystem>
#include <string>

namespace sandbox
{
    struct process_request
    {
        std::filesystem::path executable;
        std::filesystem::path working_directory;
        sandbox::config config;
        std::string stdin_data;
        std::chrono::milliseconds timeout{120000};
        bool refresh = false;
        process_results results;
    };
}
