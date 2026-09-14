#pragma once

#include "fsystem/watcher/watcher.h"

#include <cstddef>
#include <string>
#include <vector>

namespace fsystem::EventWatcher
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

    DecodedResults decode(
        const std::vector<std::vector<std::byte>>& events,
        const std::vector<std::vector<std::byte>>& file_events
    );

    DecodedResults decode(const WatcherResult& watcher_result);
}

namespace fsystem
{
    using DecodedEvent = EventWatcher::DecodedEvent;
    using DecodedResults = EventWatcher::DecodedResults;

    /* Compatibility entry point for existing callers. */
    DecodedResults decode_results(
        const std::vector<std::vector<std::byte>>& events,
        const std::vector<std::vector<std::byte>>& file_events
    );
}
