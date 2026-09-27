#include "windows_watcher_detail.h"

#include <cstddef>

namespace fsystem::windows::watcher_common::detail
{
    void parse_directory_events(
        const std::vector<std::byte>& event_buffer,
        const std::wstring& watched_file_name,
        WatcherState* state,
        bool& target_file_changed
    ) noexcept
    {
        constexpr std::size_t notify_header_size =
            offsetof(
                FILE_NOTIFY_INFORMATION,
                FileName
            );

        std::size_t event_offset = 0;

        while (
            event_offset + notify_header_size <=
            event_buffer.size()
        )
        {
            const std::size_t remaining_bytes =
                event_buffer.size() - event_offset;

            const auto* notify_information =
                reinterpret_cast<
                    const FILE_NOTIFY_INFORMATION*
                >(
                    event_buffer.data() + event_offset
                );

            const std::size_t record_size =
                notify_information->NextEntryOffset == 0
                    ? remaining_bytes
                    : notify_information->NextEntryOffset;

            if (
                record_size < notify_header_size ||
                record_size > remaining_bytes
            )
            {
                break;
            }

            const std::size_t file_name_bytes =
                notify_information->FileNameLength;

            if (
                file_name_bytes % sizeof(WCHAR) != 0 ||
                file_name_bytes >
                    record_size - notify_header_size
            )
            {
                break;
            }

            const std::size_t file_name_characters =
                file_name_bytes / sizeof(WCHAR);

            const bool is_target_file =
                file_name_characters == watched_file_name.size() &&
                CompareStringOrdinal(
                    notify_information->FileName,
                    static_cast<int>(
                        file_name_characters
                    ),
                    watched_file_name.data(),
                    static_cast<int>(
                        watched_file_name.size()
                    ),
                    TRUE
                ) == CSTR_EQUAL;

            if (is_target_file && publish_file_changed(state))
            {
                /*
                 * The watcher publishes the observable file change before
                 * signalling the edit cancellation path.
                 */
                target_file_changed = true;
                break;
            }

            if (notify_information->NextEntryOffset == 0)
                break;

            event_offset += notify_information->NextEntryOffset;
        }
    }
}
