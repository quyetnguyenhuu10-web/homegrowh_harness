#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <sandbox>
#include <registry.h>

#include "raii.h"

#include <vector>

namespace sandbox::detail::process::windows
{
    class appcontainer_security final
    {
    public:
        appcontainer_security(
            const registry_result& registry,
            network_access network);

        appcontainer_security(const appcontainer_security&) = delete;
        appcontainer_security& operator=(const appcontainer_security&) = delete;
        appcontainer_security(appcontainer_security&&) = delete;
        appcontainer_security& operator=(appcontainer_security&&) = delete;

        void apply(LPPROC_THREAD_ATTRIBUTE_LIST attributes) const;

    private:
        unique_sid appcontainer_sid_;
        std::vector<unique_local_memory> capability_sid_storage_;
        std::vector<std::vector<unsigned char>> capability_buffer_storage_;
        std::vector<SID_AND_ATTRIBUTES> capabilities_;
        SECURITY_CAPABILITIES security_{};
    };
}
