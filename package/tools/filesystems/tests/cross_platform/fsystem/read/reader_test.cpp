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
        std::cerr << "Unable to determine the temporary directory.\n";
        return 1;
    }

    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();

    const std::filesystem::path file_path = temp_directory /
        ("filesystems-reader-test-" + std::to_string(timestamp) + ".txt");

    test_support::temporary_file_guard cleanup{file_path};

    const std::string expected_content =
        "reader integration test content\n";

    if (!test_support::write_file(file_path, expected_content))
    {
        std::cerr << "Unable to create the reader integration-test file.\n";
        return 1;
    }

    const fsystem::ReadResult result = fsystem::read(file_path);

    if (result.error != 0)
    {
        std::cerr << "Read failed. Error: "
                  << result.error << '\n';
        return 1;
    }

    if (result.content != expected_content)
    {
        std::cerr << "Read returned unexpected content.\n";
        return 1;
    }

    std::cout << "Read integration test passed.\n";

    return 0;
}
