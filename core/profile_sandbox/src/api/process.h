#pragma once

#include "registry.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace sandbox
{
    enum class network_access : std::uint32_t
    {
        none = 0,
        internet_client = 1,
    };

    struct process_request
    {
        std::filesystem::path executable;
        std::vector<std::string> arguments;
        std::filesystem::path working_directory;
        std::vector<registry_request> filesystem;
        std::string stdin_data;
        std::chrono::milliseconds timeout{120000};
        network_access network = network_access::none;
        bool refresh = false;
    };

    struct process_final_state
    {
        bool started = false;
        bool timed_out = false;
        bool terminated = false;
        int exit_code = -1;

        // Snapshot taken before timeout teardown. A zero code means no OS
        // failure had been reported at that point; timed_out remains explicit.
        std::error_code os_error_before_termination;

        // Process-wide launch/supervision failure which is not attributable to
        // one registered filesystem path.
        std::error_code final_error;

        registry_result registry;
    };

    struct process_result
    {
        process_final_state state;
        std::string stdout_text;
        std::string stderr_text;
    };

    process_result process(const process_request& request);
}
