#include <fsystem>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <system_error>

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

    const std::filesystem::path unrelated_file_path = temp_directory /
        ("filesystems-watcher-unrelated-" +
         std::to_string(timestamp) + ".txt");

    temporary_file_guard cleanup{file_path};
    temporary_file_guard unrelated_cleanup{unrelated_file_path};

    if (!write_file(file_path, "before\n"))
    {
        std::cerr << "Unable to create the watcher integration-test file.\n";
        return 1;
    }

    if (!write_file(unrelated_file_path, "before\n"))
    {
        std::cerr << "Unable to create the unrelated watcher test file.\n";
        return 1;
    }

    std::atomic_bool writer_succeeded{false};

    fsystem::WatcherState watcher_state;
    fsystem::WatcherResult result{};

    std::thread watcher_thread([&]()
    {
        result = fsystem::watcher(file_path, 5000, watcher_state);
    });

    while (
        !watcher_state.ready.load(std::memory_order_acquire) &&
        !watcher_state.finished.load(std::memory_order_acquire)
    )
    {
        std::this_thread::yield();
    }

    if (
        watcher_state.finished.load(std::memory_order_acquire) &&
        !watcher_state.ready.load(std::memory_order_acquire)
    )
    {
        watcher_thread.join();
        std::cerr << "Watcher failed to start. Error: "
                  << result.error << '\n';
        return 1;
    }

    std::thread writer([&]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const bool unrelated_write =
            write_file(unrelated_file_path, "unrelated\n");

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        writer_succeeded =
            unrelated_write &&
            write_file(file_path, "after\n");
    });

    writer.join();
    watcher_thread.join();

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
              << "error: " << result.error << '\n';

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

    if (
        result.event_status != fsystem::EventStatus::HasEvent ||
        !watcher_state.file_changed.load(std::memory_order_acquire)
    )
    {
        std::cerr << "Watcher did not report the modified target file.\n";
        return 1;
    }

    return 0;
}
