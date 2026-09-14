#include "event_decoder.h"

#include <Windows.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace fsystem::windows
{
    namespace
    {
        std::string action_to_string(DWORD action)
        {
            switch (action)
            {
            case FILE_ACTION_ADDED:
                return "Added";

            case FILE_ACTION_REMOVED:
                return "Removed";

            case FILE_ACTION_MODIFIED:
                return "Modified";

            case FILE_ACTION_RENAMED_OLD_NAME:
                return "RenamedOldName";

            case FILE_ACTION_RENAMED_NEW_NAME:
                return "RenamedNewName";

            default:
                return "Unknown";
            }
        }

        std::string wide_to_utf8(
            const WCHAR* data,
            std::size_t character_count
        )
        {
            if (data == nullptr || character_count == 0)
                return {};

            const int required_size = WideCharToMultiByte(
                CP_UTF8,
                0,
                data,
                static_cast<int>(character_count),
                nullptr,
                0,
                nullptr,
                nullptr
            );

            if (required_size <= 0)
                return {};

            std::string result(
                static_cast<std::size_t>(required_size),
                '\0'
            );

            const int converted_size = WideCharToMultiByte(
                CP_UTF8,
                0,
                data,
                static_cast<int>(character_count),
                result.data(),
                required_size,
                nullptr,
                nullptr
            );

            if (converted_size <= 0)
                return {};

            result.resize(
                static_cast<std::size_t>(converted_size)
            );

            return result;
        }

        bool decode_one_record(
            const std::vector<std::byte>& raw_record,
            DecodedEvent& decoded_event
        )
        {
            constexpr std::size_t header_size =
                offsetof(FILE_NOTIFY_INFORMATION, FileName);

            if (raw_record.size() < header_size)
                return false;

            FILE_NOTIFY_INFORMATION header{};

            std::memcpy(
                &header,
                raw_record.data(),
                header_size
            );

            const std::size_t file_name_bytes =
                static_cast<std::size_t>(header.FileNameLength);

            if (file_name_bytes % sizeof(WCHAR) != 0)
                return false;

            if (
                file_name_bytes >
                raw_record.size() - header_size
            )
            {
                return false;
            }

            const auto* file_name_data =
                reinterpret_cast<const WCHAR*>(
                    raw_record.data() + header_size
                );

            decoded_event.action =
                action_to_string(header.Action);

            decoded_event.file_name =
                wide_to_utf8(
                    file_name_data,
                    file_name_bytes / sizeof(WCHAR)
                );

            return true;
        }

        void decode_single_buffer(
            const std::vector<std::byte>& buffer,
            std::vector<DecodedEvent>& output
        )
        {
            constexpr std::size_t header_size =
                offsetof(FILE_NOTIFY_INFORMATION, FileName);

            std::size_t offset = 0;

            while (
                offset + header_size <= buffer.size()
            )
            {
                const std::size_t remaining_bytes =
                    buffer.size() - offset;

                FILE_NOTIFY_INFORMATION header{};

                std::memcpy(
                    &header,
                    buffer.data() + offset,
                    header_size
                );

                const std::size_t file_name_bytes =
                    static_cast<std::size_t>(
                        header.FileNameLength
                    );

                if (file_name_bytes % sizeof(WCHAR) != 0)
                    break;

                if (
                    file_name_bytes >
                    remaining_bytes - header_size
                )
                {
                    break;
                }

                const std::size_t record_size =
                    header.NextEntryOffset == 0
                        ? remaining_bytes
                        : static_cast<std::size_t>(
                            header.NextEntryOffset
                        );

                if (
                    record_size < header_size ||
                    record_size > remaining_bytes
                )
                {
                    break;
                }

                std::vector<std::byte> record(
                    buffer.begin() +
                        static_cast<std::ptrdiff_t>(offset),

                    buffer.begin() +
                        static_cast<std::ptrdiff_t>(
                            offset + record_size
                        )
                );

                DecodedEvent decoded_event{};

                if (decode_one_record(record, decoded_event))
                {
                    output.push_back(
                        std::move(decoded_event)
                    );
                }

                if (header.NextEntryOffset == 0)
                    break;

                offset +=
                    static_cast<std::size_t>(
                        header.NextEntryOffset
                    );
            }
        }

        void decode_record_list(
            const std::vector<std::vector<std::byte>>& records,
            std::vector<DecodedEvent>& output
        )
        {
            for (const auto& record : records)
            {
                DecodedEvent decoded_event{};

                if (decode_one_record(record, decoded_event))
                {
                    output.push_back(
                        std::move(decoded_event)
                    );
                }
            }
        }
    }

    DecodedResults decode_results(
        const std::vector<std::vector<std::byte>>& events,
        const std::vector<std::vector<std::byte>>& file_events
    )
    {
        DecodedResults result{};

        /*
         * events:
         * Mỗi phần tử là một buffer lớn có thể chứa
         * nhiều FILE_NOTIFY_INFORMATION.
         */
        for (const auto& buffer : events)
        {
            decode_single_buffer(
                buffer,
                result.events
            );
        }

        /*
         * file_events:
         * Mỗi phần tử đã là một FILE_NOTIFY_INFORMATION
         * riêng biệt, nên giải mã trực tiếp.
         */
        decode_record_list(
            file_events,
            result.file_events
        );

        return result;
    }
}
