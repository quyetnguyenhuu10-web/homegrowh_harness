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
        "line 1\n"
        "line 2\n"
        "line 3\n"
        "line 4";

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

    const fsystem::ReadRequests requests{
        fsystem::ReadRequest{file_path, 2, 3},
        fsystem::ReadRequest{file_path, 4, 4},
        fsystem::ReadRequest{file_path, 10, 12},
    };

    const fsystem::ReadResults results = fsystem::read(requests);

    if (results.size() != requests.size())
    {
        std::cerr << "Batch read returned unexpected result count.\n";
        return 1;
    }

    if (
        results[0].error != 0 ||
        results[0].path != file_path ||
        results[0].start_line != 2 ||
        results[0].end_line != 3 ||
        results[0].content != "line 2\nline 3\n"
    )
    {
        std::cerr << "Batch read returned unexpected lines 2-3.\n";
        return 1;
    }

    if (
        results[1].error != 0 ||
        results[1].content != "line 4"
    )
    {
        std::cerr << "Batch read returned unexpected final line.\n";
        return 1;
    }

    if (
        results[2].error != 0 ||
        !results[2].content.empty()
    )
    {
        std::cerr << "Batch read past EOF should return empty content.\n";
        return 1;
    }

    const fsystem::ReadResult ranged = fsystem::read(file_path, 1, 1);

    if (ranged.error != 0 || ranged.content != "line 1\n")
    {
        std::cerr << "Single ranged read returned unexpected content.\n";
        return 1;
    }

    const fsystem::ReadResults invalid_results = fsystem::read(
        fsystem::ReadRequests{
            fsystem::ReadRequest{file_path, 0, 1},
            fsystem::ReadRequest{file_path, 3, 2},
        }
    );

    if (
        invalid_results.size() != 2 ||
        invalid_results[0].error == 0 ||
        invalid_results[1].error == 0
    )
    {
        std::cerr << "Invalid line ranges were not rejected.\n";
        return 1;
    }

    std::cout << "Read integration test passed.\n";

    return 0;
}
