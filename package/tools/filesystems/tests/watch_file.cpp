#include <fsystem>
#include <cstddef>
#include <cstdint>
#include <iostream>

#if defined(_WIN32)
#include <Windows.h>

namespace
{
    const wchar_t* event_action_name(DWORD action)
    {
        switch (action)
        {
        case FILE_ACTION_ADDED:
            return L"ADDED";
        case FILE_ACTION_REMOVED:
            return L"REMOVED";
        case FILE_ACTION_MODIFIED:
            return L"MODIFIED";
        case FILE_ACTION_RENAMED_OLD_NAME:
            return L"RENAMED_OLD_NAME";
        case FILE_ACTION_RENAMED_NEW_NAME:
            return L"RENAMED_NEW_NAME";
        default:
            return L"UNKNOWN";
        }
    }

    bool print_event_buffer(
        const std::vector<std::byte>& buffer,
        const wchar_t* group_name,
        std::size_t buffer_index
    )
    {
        constexpr std::size_t notify_header_size =
            offsetof(FILE_NOTIFY_INFORMATION, FileName);
        std::size_t event_offset = 0;
        bool printed_event = false;

        while (event_offset + notify_header_size <= buffer.size())
        {
            const auto* notify_information =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    buffer.data() + event_offset
                );

            const std::size_t file_name_bytes =
                notify_information->FileNameLength;
            const std::size_t record_size =
                notify_information->NextEntryOffset == 0
                    ? buffer.size() - event_offset
                    : notify_information->NextEntryOffset;

            if (record_size < notify_header_size ||
                record_size > buffer.size() - event_offset ||
                file_name_bytes % sizeof(WCHAR) != 0 ||
                file_name_bytes > record_size - notify_header_size)
            {
                std::wcout << L"[" << group_name << L" #" << buffer_index
                           << L"] malformed event record\n";
                return false;
            }

            const std::wstring file_name(
                notify_information->FileName,
                file_name_bytes / sizeof(WCHAR)
            );

            std::wcout << L"[" << group_name << L" #" << buffer_index << L"] "
                       << event_action_name(notify_information->Action)
                       << L" - " << file_name << L"\n";
            printed_event = true;

            if (notify_information->NextEntryOffset == 0)
                break;

            event_offset += notify_information->NextEntryOffset;
        }

        if (!printed_event)
        {
            std::wcout << L"[" << group_name << L" #" << buffer_index
                       << L"] no event record\n";
        }

        return true;
    }
}
#endif

int main()
{
    auto result = fsystem::watcher(
        R"(D:\homegrowh_harness\package\tools\filesystems\tests\tool_call.txt)",
        10000
    );

    const char* status_name = "NONE";
    switch (result.event_status)
    {
    case fsystem::EventStatus::HasEvent:
        status_name = "HAS_EVENT";
        break;
    case fsystem::EventStatus::NoEvent:
        status_name = "NO_EVENT";
        break;
    case fsystem::EventStatus::None:
        break;
    }

    std::cout << "status: " << status_name << '\n'
              << "error: " << result.error << '\n'
              << "event buffers: " << result.events.size() << '\n'
              << "file event records: " << result.file_events.size() << '\n';

#if defined(_WIN32)
    std::wcout << L"\nAll event records:\n";
    for (std::size_t index = 0; index < result.events.size(); ++index)
    {
        print_event_buffer(result.events[index], L"all", index);
    }

    std::wcout << L"\nMatching file event records:\n";
    for (std::size_t index = 0; index < result.file_events.size(); ++index)
    {
        print_event_buffer(result.file_events[index], L"file", index);
    }
#endif

    return 0;
}
