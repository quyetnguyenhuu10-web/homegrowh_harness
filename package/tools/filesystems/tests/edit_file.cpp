#include <file/edit.h>
#include <file/reader.h>

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

    bool expect(bool condition, const std::string& message)
    {
        if (condition)
            return true;

        std::cerr << message << '\n';
        return false;
    }
}

int main()
{
    std::error_code filesystem_error;
    const std::filesystem::path temp_directory =
        std::filesystem::temp_directory_path(filesystem_error);

    if (!expect(
            !filesystem_error,
            "Unable to determine the temporary directory."
        ))
    {
        return 1;
    }

    const auto timestamp =
        std::chrono::steady_clock::now().time_since_epoch().count();

    const std::filesystem::path test_path = temp_directory /
        ("filesystems-edit-test-" + std::to_string(timestamp) + ".txt");

    temporary_file_guard cleanup{test_path};

    const std::string old_data = "old_data=hello";
    const std::string new_data = "old_data=world";
    const std::string original_content =
        "before\n" + old_data + "\nafter\n";
    const std::string expected_content =
        "before\n" + new_data + "\nafter\n";

    if (!expect(
            write_file(test_path, original_content),
            "Unable to create the edit integration-test file."
        ))
    {
        return 1;
    }

    const file::EditResult successful_edit = file::edit(
        test_path,
        old_data,
        new_data
    );

    if (!expect(
            successful_edit.error == 0,
            "Edit failed. Error: " +
                std::to_string(successful_edit.error)
        ) ||
        !expect(
            successful_edit.note == file::EditNote::none,
            "Successful edit returned an unexpected note."
        ) ||
        !expect(
            successful_edit.new_content == expected_content,
            "Edit returned unexpected new content."
        ))
    {
        return 1;
    }

    const file::ReadResult edited_file = file::read(test_path);

    if (!expect(
            edited_file.error == 0,
            "Unable to read the edited file. Error: " +
                std::to_string(edited_file.error)
        ) ||
        !expect(
            edited_file.content == expected_content,
            "The file on disk does not contain the expected edit."
        ))
    {
        return 1;
    }

    const file::EditResult missing_old_data = file::edit(
        test_path,
        "old_data=does-not-exist",
        new_data
    );

    if (!expect(
            missing_old_data.error == 0,
            "Missing-old-data case returned an unexpected error."
        ) ||
        !expect(
            missing_old_data.note == file::EditNote::old_data_not_found,
            "Missing-old-data case returned the wrong note."
        ))
    {
        return 1;
    }

    const std::string duplicate_content =
        old_data + "\nseparator\n" + old_data + "\n";

    if (!expect(
            write_file(test_path, duplicate_content),
            "Unable to prepare the duplicate-old-data case."
        ))
    {
        return 1;
    }

    const file::EditResult duplicate_old_data = file::edit(
        test_path,
        old_data,
        new_data
    );

    if (!expect(
            duplicate_old_data.error == 0,
            "Duplicate-old-data case returned an unexpected error."
        ) ||
        !expect(
            duplicate_old_data.note ==
                file::EditNote::old_data_appears_more_than_once,
            "Duplicate-old-data case returned the wrong note."
        ))
    {
        return 1;
    }

    const file::ReadResult duplicate_file = file::read(test_path);

    if (!expect(
            duplicate_file.error == 0,
            "Unable to read the duplicate-case file."
        ) ||
        !expect(
            duplicate_file.content == duplicate_content,
            "Duplicate-old-data case modified the file unexpectedly."
        ))
    {
        return 1;
    }

    std::cout << "Edit integration test passed.\n";
    return 0;
}
