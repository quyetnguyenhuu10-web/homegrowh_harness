#include <fsystem>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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
        const std::string& content
    )
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);

        if (!output)
            return false;

        output.write(
            content.data(),
            static_cast<std::streamsize>(content.size())
        );

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
        ("filesystems-reader-test-" + std::to_string(timestamp) + ".txt");

    temporary_file_guard cleanup{file_path};

    const std::string expected_content =
        "reader integration test content\n";

    if (!write_file(file_path, expected_content))
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
