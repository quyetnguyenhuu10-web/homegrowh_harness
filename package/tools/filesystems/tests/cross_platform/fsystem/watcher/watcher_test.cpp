#include <fsystem>
#include <test_support.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <thread>
#include <system_error>

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

    test_support::temporary_file_guard cleanup{file_path};
    test_support::temporary_file_guard unrelated_cleanup{
        unrelated_file_path
    };

    if (!test_support::write_file(file_path, "before\n"))
    {
        std::cerr << "Unable to create the watcher integration-test file.\n";
        return 1;
    }

    if (!test_support::write_file(unrelated_file_path, "before\n"))
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
            test_support::write_file(
                unrelated_file_path,
                "unrelated\n"
            );

        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        writer_succeeded =
            unrelated_write &&
            test_support::write_file(file_path, "after\n");
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

    fsystem::WatcherState cancellation_state;
    fsystem::WatcherResult cancellation_result{};

    std::thread cancellation_thread([&]()
    {
        cancellation_result = fsystem::watcher(
            unrelated_file_path,
            std::numeric_limits<int>::max(),
            cancellation_state
        );
    });

    while (
        !cancellation_state.ready.load(std::memory_order_acquire) &&
        !cancellation_state.finished.load(std::memory_order_acquire)
    )
    {
        std::this_thread::yield();
    }

    if (
        cancellation_state.finished.load(std::memory_order_acquire) &&
        !cancellation_state.ready.load(std::memory_order_acquire)
    )
    {
        cancellation_thread.join();
        std::cerr << "Cancellation watcher failed to start. Error: "
                  << cancellation_result.error << '\n';
        return 1;
    }

    fsystem::request_watcher_stop(cancellation_state);
    cancellation_thread.join();

    if (
        cancellation_result.error != 0 ||
        cancellation_result.event_status != fsystem::EventStatus::NoEvent
    )
    {
        std::cerr << "Watcher cancellation returned an unexpected result. "
                  << "Error: " << cancellation_result.error << '\n';
        return 1;
    }

    return 0;
}
