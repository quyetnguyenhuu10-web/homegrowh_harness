#include <fsystem>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <system_error>

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

namespace
{
    struct temporary_file_guard
    {
        std::filesystem::path path;

        ~temporary_file_guard() noexcept
        {
            std::error_code error;
            (void)std::filesystem::remove(path, error);
        }
    };

    bool write_file(
        const std::filesystem::path& path,
        const char* content
    )
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);

        if (!output)
            return false;

        output << content;
        return output.good();
    }
}

int main()
{
    std::error_code filesystem_error;
    const std::filesystem::path temp_directory =
        std::filesystem::temp_directory_path(filesystem_error);

    if (filesystem_error)
    {
        std::cerr << "Unable to determine the temporary directory.\n";
        return 1;
    }

    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();

    const std::filesystem::path file_path = temp_directory /
        ("filesystems-watcher-test-" + std::to_string(timestamp) + ".txt");

    temporary_file_guard cleanup{file_path};

    if (!write_file(file_path, "before\n"))
    {
        std::cerr << "Unable to create the watcher integration-test file.\n";
        return 1;
    }

    std::atomic_bool writer_succeeded{false};

    std::thread writer([&]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        writer_succeeded = write_file(file_path, "after\n");
    });

    const auto result = fsystem::watcher(file_path, 5000);

    writer.join();

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

    if (!writer_succeeded)
    {
        std::cerr << "Unable to modify the watcher integration-test file.\n";
        return 1;
    }

    if (result.error != 0)
    {
        std::cerr << "Watcher returned an error: " << result.error << '\n';
        return 1;
    }

    if (result.event_status != fsystem::EventStatus::HasEvent ||
        result.events.empty() ||
        result.file_events.empty())
    {
        std::cerr << "Watcher did not report the modified target file.\n";
        return 1;
    }

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
