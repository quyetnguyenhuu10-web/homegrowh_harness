#include <fsystem>
#include <test_support.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

int main()
{
    std::error_code filesystem_error;
    const std::filesystem::path temp_directory =
        std::filesystem::temp_directory_path(filesystem_error);

    if (filesystem_error)
    {
        std::cerr << "Unable to determine temporary directory.\n";
        return 1;
    }

    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();

    const std::filesystem::path file_a = temp_directory /
        ("filesystems-write-a-" + std::to_string(timestamp) + ".txt");
    const std::filesystem::path file_b = temp_directory /
        ("filesystems-write-b-" + std::to_string(timestamp) + ".txt");

    test_support::temporary_file_guard cleanup_a{file_a};
    test_support::temporary_file_guard cleanup_b{file_b};

    const std::string initial = "old content that is longer";
    const fsystem::WriteResult created = fsystem::write(file_a, initial);

    if (created.error != 0)
    {
        std::cerr << "Single write failed to create file.\n";
        return 1;
    }

    const std::string replacement = "short";
    const fsystem::WriteResult overwritten = fsystem::write(file_a, replacement);

    if (overwritten.error != 0)
    {
        std::cerr << "Single write failed to overwrite file.\n";
        return 1;
    }

    const fsystem::ReadResult read_a = fsystem::read(file_a);
    if (read_a.error != 0 || read_a.content != replacement)
    {
        std::cerr << "Overwrite did not truncate old content.\n";
        return 1;
    }

    const std::string batch_a_content = "batch a";
    const std::string batch_b_content = "batch b\nline 2";

    const fsystem::WriteRequests requests{
        fsystem::WriteRequest{file_a, batch_a_content},
        fsystem::WriteRequest{file_b, batch_b_content},
    };

    const fsystem::WriteResults results = fsystem::write(requests);
    if (
        results.size() != requests.size() ||
        results[0].error != 0 ||
        results[1].error != 0
    )
    {
        std::cerr << "Batch write failed.\n";
        return 1;
    }

    const fsystem::ReadResult batch_a = fsystem::read(file_a);
    const fsystem::ReadResult batch_b = fsystem::read(file_b);

    if (
        batch_a.error != 0 ||
        batch_b.error != 0 ||
        batch_a.content != batch_a_content ||
        batch_b.content != batch_b_content
    )
    {
        std::cerr << "Batch write returned unexpected file contents.\n";
        return 1;
    }

    std::cout << "Write integration test passed.\n";
    return 0;
}
