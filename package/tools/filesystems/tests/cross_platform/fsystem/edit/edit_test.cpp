#include <fsystem>
#include <fsystem/edit/edit_detail.h>
#include <test_support.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <string>
#include <system_error>
#include <string_view>

namespace
{
    bool expect(bool condition, const std::string& message)
    {
        if (condition)
            return true;

        std::cerr << message << '\n';
        return false;
    }

    bool expect_chunk_matcher(
        const std::string& pattern,
        std::initializer_list<std::string_view> chunks,
        std::uint64_t expected_occurrence,
        fsystem::EditNote expected_note,
        const std::string& case_name
    )
    {
        fsystem::detail::chunk_matcher matcher(pattern);
        std::uint64_t offset = 0;

        for (const std::string_view chunk : chunks)
        {
            const bool consumed = matcher.consume(offset, chunk);
            offset += chunk.size();

            if (!consumed)
            {
                if (!matcher.duplicate())
                {
                    return expect(
                        false,
                        "Chunk matcher stopped unexpectedly: " +
                            case_name
                    );
                }

                break;
            }
        }

        matcher.finish(offset);

        return expect(
                matcher.note() == expected_note,
                "Chunk matcher returned the wrong note: " + case_name
            ) &&
            expect(
                matcher.first_occurrence() == expected_occurrence,
                "Chunk matcher returned the wrong occurrence: " +
                    case_name
            );
    }
}

int main()
{
    if (!expect_chunk_matcher(
            "CDEF",
            {"xxC", "DEFX"},
            2,
            fsystem::EditNote::none,
            "match completed across a chunk boundary"
        ) ||
        !expect_chunk_matcher(
            "ABCDEFG",
            {"ABC", "DEF", "G"},
            0,
            fsystem::EditNote::none,
            "pattern longer than each chunk"
        ) ||
        !expect_chunk_matcher(
            "needle",
            {"needle|nee", "dle"},
            0,
            fsystem::EditNote::old_data_appears_more_than_once,
            "duplicate with a cross-boundary second match"
        ) ||
        !expect_chunk_matcher(
            "abcdef",
            {"abc", "xdef"},
            fsystem::detail::chunk_matcher::no_occurrence,
            fsystem::EditNote::old_data_not_found,
            "partial checkpoint discarded after a mismatch"
        ) ||
        !expect_chunk_matcher(
            "aaa",
            {"aa", "aa"},
            0,
            fsystem::EditNote::old_data_occurrences_overlap,
            "overlapping cross-boundary matches"
        ) ||
        !expect_chunk_matcher(
            "ABA",
            {"AB", "ABA"},
            0,
            fsystem::EditNote::old_data_occurrences_overlap,
            "overlapping ABA matches across a chunk boundary"
        ))
    {
        return 1;
    }

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

    test_support::temporary_file_guard cleanup{test_path};

    const std::string old_data = "old_data=hello";
    const std::string new_data = "old_data=world";
    const std::string original_content =
        "before\n" + old_data + "\nafter\n";
    const std::string expected_content =
        "before\n" + new_data + "\nafter\n";

    if (!expect(
            test_support::write_file(test_path, original_content),
            "Unable to create the edit integration-test file."
        ))
    {
        return 1;
    }

    const fsystem::EditResult successful_edit = fsystem::edit(
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
            successful_edit.note == fsystem::EditNote::none,
            "Successful edit returned an unexpected note."
        ))
    {
        return 1;
    }

    const fsystem::ReadResult edited_file = fsystem::read(test_path);

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

    const std::string boundary_old_data = "boundary-old";
    const std::string boundary_new_data = "boundary-new";
    const std::string boundary_content =
        std::string(65535, 'p') + boundary_old_data + "\nboundary-tail";
    const std::string boundary_expected_content =
        std::string(65535, 'p') + boundary_new_data + "\nboundary-tail";

    if (!expect(
            test_support::write_file(test_path, boundary_content),
            "Unable to prepare the chunk-boundary edit case."
        ))
    {
        return 1;
    }

    const fsystem::EditResult boundary_edit = fsystem::edit(
        test_path,
        boundary_old_data,
        boundary_new_data
    );

    if (!expect(
            boundary_edit.error == 0,
            "Chunk-boundary edit failed. Error: " +
                std::to_string(boundary_edit.error)
        ) ||
        !expect(
            boundary_edit.note == fsystem::EditNote::none,
            "Chunk-boundary edit returned an unexpected note."
        ))
    {
        return 1;
    }

    const fsystem::ReadResult boundary_file = fsystem::read(test_path);

    if (!expect(
            boundary_file.error == 0,
            "Unable to read the chunk-boundary edit result."
        ) ||
        !expect(
            boundary_file.content == boundary_expected_content,
            "Chunk-boundary edit produced unexpected file content."
        ))
    {
        return 1;
    }

    const fsystem::EditResult missing_old_data = fsystem::edit(
        test_path,
        "old_data=does-not-exist",
        new_data
    );

    if (!expect(
            missing_old_data.error == 0,
            "Missing-old-data case returned an unexpected error."
        ) ||
        !expect(
            missing_old_data.note == fsystem::EditNote::old_data_not_found,
            "Missing-old-data case returned the wrong note."
        ))
    {
        return 1;
    }

    const std::string duplicate_content =
        old_data + "\nseparator\n" + old_data + "\n";

    if (!expect(
            test_support::write_file(test_path, duplicate_content),
            "Unable to prepare the duplicate-old-data case."
        ))
    {
        return 1;
    }

    const fsystem::EditResult duplicate_old_data = fsystem::edit(
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
                fsystem::EditNote::old_data_appears_more_than_once,
            "Duplicate-old-data case returned the wrong note."
        ))
    {
        return 1;
    }

    const fsystem::ReadResult duplicate_file = fsystem::read(test_path);

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

    const std::string overlapping_content = "ABABA";

    if (!expect(
            test_support::write_file(test_path, overlapping_content),
            "Unable to prepare the overlapping-old-data case."
        ))
    {
        return 1;
    }

    const fsystem::EditResult overlapping_old_data = fsystem::edit(
        test_path,
        "ABA",
        "replacement"
    );

    if (!expect(
            overlapping_old_data.error == 0,
            "Overlapping-old-data case returned an unexpected error."
        ) ||
        !expect(
            overlapping_old_data.note ==
                fsystem::EditNote::old_data_occurrences_overlap,
            "Overlapping-old-data case returned the wrong note."
        ) ||
        !expect(
            !overlapping_old_data.replace_attempted,
            "Overlapping-old-data case reached replacement unexpectedly."
        ))
    {
        return 1;
    }

    const fsystem::ReadResult overlapping_file = fsystem::read(test_path);

    if (!expect(
            overlapping_file.error == 0,
            "Unable to read the overlapping-case file."
        ) ||
        !expect(
            overlapping_file.content == overlapping_content,
            "Overlapping-old-data case modified the file unexpectedly."
        ))
    {
        return 1;
    }

    if (!expect(
            test_support::write_file(test_path, ""),
            "Unable to prepare the empty-file edit case."
        ))
    {
        return 1;
    }

    const fsystem::EditResult empty_file_edit = fsystem::edit(
        test_path,
        "",
        "empty-file-replacement"
    );

    if (!expect(
            empty_file_edit.error == 0,
            "Empty-file edit returned an unexpected error."
        ) ||
        !expect(
            empty_file_edit.note == fsystem::EditNote::none,
            "Empty-file edit returned an unexpected note."
        ))
    {
        return 1;
    }

    const fsystem::ReadResult empty_file_result = fsystem::read(test_path);

    if (!expect(
            empty_file_result.error == 0,
            "Unable to read the empty-file edit result."
        ) ||
        !expect(
            empty_file_result.content == "empty-file-replacement",
            "Empty-file edit produced unexpected content."
        ))
    {
        return 1;
    }

    std::cout << "Edit integration test passed.\n";
    return 0;
}
