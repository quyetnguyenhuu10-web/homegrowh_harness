#include "event_decoder.h"

#include <sys/inotify.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace fsystem::linux
{
    namespace
    {
        std::string mask_to_string(std::uint32_t mask)
        {
            std::string result;

            auto append = [&result](const char* value)
            {
                if (!result.empty())
                    result += " | ";

                result += value;
            };

            if (mask & IN_ACCESS)
                append("Access");

            if (mask & IN_MODIFY)
                append("Modify");

            if (mask & IN_ATTRIB)
                append("Attrib");

            if (mask & IN_CLOSE_WRITE)
                append("CloseWrite");

            if (mask & IN_CLOSE_NOWRITE)
                append("CloseNoWrite");

            if (mask & IN_OPEN)
                append("Open");

            if (mask & IN_MOVED_FROM)
                append("MovedFrom");

            if (mask & IN_MOVED_TO)
                append("MovedTo");

            if (mask & IN_CREATE)
                append("Create");

            if (mask & IN_DELETE)
                append("Delete");

            if (mask & IN_DELETE_SELF)
                append("DeleteSelf");

            if (mask & IN_MOVE_SELF)
                append("MoveSelf");

            if (mask & IN_UNMOUNT)
                append("Unmount");

            if (mask & IN_Q_OVERFLOW)
                append("QueueOverflow");

            if (mask & IN_IGNORED)
                append("Ignored");

            if (mask & IN_ISDIR)
                append("Directory");

            if (mask & IN_ONESHOT)
                append("OneShot");

            if (mask & IN_ONLYDIR)
                append("OnlyDirectory");

            if (result.empty())
                result = "Unknown";

            return result;
        }

        bool decode_one_record(
            const std::vector<std::byte>& raw_record,
            DecodedEvent& decoded_event
        )
        {
            constexpr std::size_t header_size =
                sizeof(struct inotify_event);

            if (raw_record.size() < header_size)
                return false;

            struct inotify_event header{};

            std::memcpy(
                &header,
                raw_record.data(),
                header_size
            );

            const std::size_t name_bytes =
                static_cast<std::size_t>(header.len);

            if (
                name_bytes >
                raw_record.size() - header_size
            )
            {
                return false;
            }

            const auto* name_data =
                reinterpret_cast<const char*>(
                    raw_record.data() + header_size
                );

            /*
             * inotify_event::len bao gồm vùng tên file,
             * thường có cả byte '\0' kết thúc.
             */
            std::size_t name_length = 0;

            while (
                name_length < name_bytes &&
                name_data[name_length] != '\0'
            )
            {
                ++name_length;
            }

            decoded_event.action =
                mask_to_string(header.mask);

            decoded_event.file_name =
                std::string(
                    name_data,
                    name_length
                );

            return true;
        }

        void decode_single_buffer(
            const std::vector<std::byte>& buffer,
            std::vector<DecodedEvent>& output
        )
        {
            constexpr std::size_t header_size =
                sizeof(struct inotify_event);

            std::size_t offset = 0;

            while (
                offset + header_size <= buffer.size()
            )
            {
                const std::size_t remaining_bytes =
                    buffer.size() - offset;

                struct inotify_event header{};

                std::memcpy(
                    &header,
                    buffer.data() + offset,
                    header_size
                );

                const std::size_t record_size =
                    header_size +
                    static_cast<std::size_t>(header.len);

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

                offset += record_size;
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
         * Mỗi phần tử là một buffer đọc từ inotify.
         * Một buffer có thể chứa nhiều inotify_event.
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
         * Mỗi phần tử đã là một inotify_event riêng biệt
         * được watcher lọc theo tên file.
         */
        decode_record_list(
            file_events,
            result.file_events
        );

        return result;
    }
}
