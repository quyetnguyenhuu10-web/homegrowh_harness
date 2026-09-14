#pragma once

#include "fsystem/watcher/watcher.h"

#include <string>
#include <vector>
#include <cstddef>

namespace fsystem::windows
{
    struct DecodedEvent
    {
        std::string action;
        std::string file_name;
    };

    struct DecodedResults
    {
        std::vector<DecodedEvent> events;
        std::vector<DecodedEvent> file_events;
    };

    DecodedResults decode_results(
        const std::vector<std::vector<std::byte>>& events,
        const std::vector<std::vector<std::byte>>& file_events
    );
}
