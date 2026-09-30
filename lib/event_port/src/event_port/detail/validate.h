#pragma once

#include <event_port>

namespace event_port::detail
{
    Result<void> validate_event(
        const std::string& package,
        const std::string& type,
        const References& references);

    Result<void> validate_registration(const References& references);
}
