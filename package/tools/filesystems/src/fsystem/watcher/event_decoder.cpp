#include "fsystem/watcher/event_decoder.h"

#if defined(_WIN32)

#include "../../platform/windows/fsystem/watcher/event_decoder.h"

#elif defined(__linux__)

#include "../../platform/linux/fsystem/watcher/event_decoder.h"

#else

#error "Unsupported operating system"

#endif

namespace fsystem::EventWatcher
{
    namespace
    {
        template <typename PlatformResults>
        DecodedResults to_public_results(
            const PlatformResults& platform_results
        )
        {
            DecodedResults result{};

            result.events.reserve(
                platform_results.events.size()
            );

            for (const auto& event : platform_results.events)
            {
                result.events.push_back(
                    DecodedEvent{
                        event.action,
                        event.file_name
                    }
                );
            }

            result.file_events.reserve(
                platform_results.file_events.size()
            );

            for (const auto& event : platform_results.file_events)
            {
                result.file_events.push_back(
                    DecodedEvent{
                        event.action,
                        event.file_name
                    }
                );
            }

            return result;
        }
    }

    DecodedResults decode(
        const std::vector<std::vector<std::byte>>& events,
        const std::vector<std::vector<std::byte>>& file_events
    )
    {
#if defined(_WIN32)
        const auto platform_results = windows::decode_results(
#elif defined(__linux__)
        const auto platform_results = linux::decode_results(
#endif
            events,
            file_events
        );

        return to_public_results(platform_results);
    }

    DecodedResults decode(const WatcherResult& watcher_result)
    {
        return decode(
            watcher_result.events,
            watcher_result.file_events
        );
    }
}

namespace fsystem
{
    DecodedResults decode_results(
        const std::vector<std::vector<std::byte>>& events,
        const std::vector<std::vector<std::byte>>& file_events
    )
    {
        return EventWatcher::decode(events, file_events);
    }
}
